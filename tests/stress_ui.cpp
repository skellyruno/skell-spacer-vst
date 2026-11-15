// Editor/processor torture test (offscreen): parameter storms from the message thread while the audio thread
// runs, rapid control toggling, editor create/destroy, every size, hostile saved states, bus layouts.
//   built and run by tests/ui_render.sh
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <atomic>
#include <cstdio>
#include <thread>

namespace juce {
void privatePostSystemNotification(const String&, const String&) {}
bool juce_handleXEmbedEvent(ComponentPeer*, void*) { return false; }
unsigned long juce_getCurrentFocusWindow(ComponentPeer*) { return 0; }
}

static int failures = 0;
static void check(bool ok, const juce::String& what) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (!ok) ++failures; }
static void pump(int ms = 30) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

int main() {
  juce::ScopedJuceInitialiser_GUI init;
  SkellAudioProcessor proc; proc.enableAllBuses(); proc.setRateAndBufferSizeDetails(48000.0, 256); proc.prepareToPlay(48000.0, 256);

  // ---- audio thread runs flat out while the message thread hammers parameters, buttons and editors
  std::atomic<bool> stop{ false }; std::atomic<long> blocks{ 0 }; std::atomic<bool> bad{ false };
  std::thread audio([&] {
    juce::AudioBuffer<float> b(4, 256); juce::MidiBuffer mi; juce::Random r(5); float ph = 0.f;
    while (!stop.load()) {
      for (int i = 0; i < 256; ++i) {
        const float t = 0.3f * std::sin(ph), k = 0.4f * std::sin(ph * 1.7f); ph += 0.05f;
        b.setSample(0, i, t + 0.1f * (r.nextFloat() - 0.5f)); b.setSample(1, i, t); b.setSample(2, i, k); b.setSample(3, i, k);
      }
      proc.processBlock(b, mi);
      for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) if (!std::isfinite(b.getSample(c, i)) || std::abs(b.getSample(c, i)) > 4.f) bad = true;
      ++blocks;
    }
  });

  juce::Random rnd(11);
  auto params = proc.getParameters();
  for (int round = 0; round < 40; ++round) {
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(juce::jlimit(396, 990, 396 + (int) rnd.nextInt(600)), juce::jlimit(324, 810, 324 + (int) rnd.nextInt(500)));
    for (int k = 0; k < 150; ++k) {
      auto* p = params[(int) rnd.nextInt(params.size())];
      
      p->setValueNotifyingHost(rnd.nextFloat());
      if (k % 25 == 0) { proc.setTheme((int) rnd.nextInt(4)); pump(5); }
    }
    pump(40);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    if (img.isNull()) { check(false, "editor snapshot failed"); break; }
  }
  check(true, "40 editors created, resized, driven and destroyed under audio load");

  // click every button many times
  {
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor()); ed->setSize(550, 450);
    int clicks = 0;
    for (int k = 0; k < 200; ++k) for (auto* c : ed->getChildren()) if (auto* b = dynamic_cast<juce::Button*>(c)) {
      b->triggerClick(); ++clicks; if (k % 7 == 0) pump(3);
    }
    pump(100);
    check(clicks > 800, "buttons clicked " + juce::String(clicks) + " times without trouble");
  }

  stop = true; audio.join();
  check(!bad.load(), "audio thread output stayed finite and within +/-4 for " + juce::String((long) blocks.load()) + " blocks of parameter chaos");
  check(blocks.load() > 100, "audio thread kept running");

  // ---- hostile / old / truncated saved states
  {
    juce::MemoryBlock good; proc.getStateInformation(good);
    const uint8_t junk[] = { 1, 2, 3, 4, 5, 250, 251, 0, 0 };
    proc.setStateInformation(nullptr, 0);
    proc.setStateInformation(junk, (int) sizeof junk);
    proc.setStateInformation(good.getData(), (int) good.getSize() / 2);          // truncated
    juce::XmlElement wrong("SOMETHINGELSE"); juce::MemoryBlock wb; juce::AudioProcessor::copyXmlToBinary(wrong, wb);
    proc.setStateInformation(wb.getData(), (int) wb.getSize());
    // a project saved by an older version: has a "link" param we no longer have, a missing "pan", and a crazy theme
    auto xml = juce::parseXML(proc.parameters.copyState().createXml()->toString());
    xml->setAttribute("theme", 999);
    auto* old = xml->createNewChildElement("PARAM"); old->setAttribute("id", "link"); old->setAttribute("value", 40.0);
    juce::MemoryBlock ob; juce::AudioProcessor::copyXmlToBinary(*xml, ob);
    proc.setStateInformation(ob.getData(), (int) ob.getSize());
    check(proc.getTheme() >= 0 && proc.getTheme() <= 3, "bad saved states (null, junk, truncated, wrong tag, old 'link', theme 999) are survived");
    proc.setStateInformation(good.getData(), (int) good.getSize());
    juce::AudioBuffer<float> b(4, 512); b.clear(); juce::MidiBuffer mi; proc.processBlock(b, mi);
    check(true, "still processes after loading junk");
  }

  // ---- bus layouts and unusual block/sample-rate setups
  {
    using CS = juce::AudioChannelSet;
    auto layout = [](CS mainIn, CS mainOut, CS sc) { SkellAudioProcessor::BusesLayout l; l.inputBuses.add(mainIn); l.inputBuses.add(sc); l.outputBuses.add(mainOut); return l; };
    check(proc.checkBusesLayoutSupported(layout(CS::stereo(), CS::stereo(), CS::stereo())), "stereo + stereo sidechain accepted");
    check(proc.checkBusesLayoutSupported(layout(CS::mono(), CS::mono(), CS::mono())), "mono + mono sidechain accepted");
    check(proc.checkBusesLayoutSupported(layout(CS::stereo(), CS::stereo(), CS::disabled())), "stereo, no sidechain accepted");
    check(!proc.checkBusesLayoutSupported(layout(CS::create5point1(), CS::create5point1(), CS::stereo())), "5.1 refused");
    check(!proc.checkBusesLayoutSupported(layout(CS::stereo(), CS::mono(), CS::stereo())), "mismatched in/out refused");
    for (double fs : { 8000.0, 22050.0, 44100.0, 96000.0, 192000.0, 384000.0 })
      for (int bs : { 1, 16, 64, 480, 8192 }) {
        proc.setRateAndBufferSizeDetails(fs, bs); proc.prepareToPlay(fs, bs);
        juce::AudioBuffer<float> b(4, bs); juce::Random r(3); juce::MidiBuffer mi; bool ok = true;
        for (int rep = 0; rep < 8; ++rep) {
          for (int c = 0; c < 4; ++c) for (int i = 0; i < bs; ++i) b.setSample(c, i, 0.5f * (r.nextFloat() - 0.5f));
          proc.processBlock(b, mi);
          for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) ok = ok && std::isfinite(b.getSample(c, i));
        }
        if (!ok) check(false, "non-finite at " + juce::String(fs) + " Hz / " + juce::String(bs));
      }
    check(true, "prepareToPlay + processBlock at 6 sample rates x 5 block sizes");
    proc.setRateAndBufferSizeDetails(48000.0, 256); proc.prepareToPlay(48000.0, 256);
    // processBlock with fewer channels than buses claim, zero-length blocks
    juce::AudioBuffer<float> mono(1, 128); mono.clear(); juce::MidiBuffer mi; proc.processBlock(mono, mi);
    juce::AudioBuffer<float> empty(4, 0); proc.processBlock(empty, mi);
    check(true, "mono buffer and zero-length block are handled");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
