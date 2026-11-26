// Opt-in offline demonstration, built only with the existing editor-audit target.
// Real JUCE slider notifications, processor blocks and component snapshots.
// The input is deliberately synthetic; no claim of a musical or live DAW capture.
#pragma once
#include <cmath>
#include <cstdint>
#include <memory>

namespace spacer_demo {
constexpr int rate=48000, seconds=16, sampleCount=rate*seconds;
struct Wav {
  juce::FileOutputStream stream;
  explicit Wav(const juce::File& path):stream(path) {}
  bool header() {
    return stream.openedOk() && stream.write("RIFF",4) && stream.writeInt(36+2*sampleCount)
      && stream.write("WAVEfmt ",8) && stream.writeInt(16) && stream.writeShort(1)
      && stream.writeShort(1) && stream.writeInt(rate) && stream.writeInt(2*rate)
      && stream.writeShort(2) && stream.writeShort(16) && stream.write("data",4)
      && stream.writeInt(2*sampleCount);
  }
  bool sample(float x, double& value) {
    if (!std::isfinite(x) || x < -1.f || x >= 1.f) return false;
    const auto q=static_cast<short>(juce::jlimit(-32768,32767,(int)std::lround(x*32768.0)));
    value=static_cast<double>(q)/32768.0;
    return stream.writeShort(q);
  }
};
struct Meter {
  double squares=0., peak=0.;
  void add(double x) { squares+=x*x;peak=juce::jmax(peak,std::abs(x)); }
  double rms(int samples) const { return std::sqrt(squares/samples); }
};
static bool run(const juce::File& out, const juce::String& revision, int fps=24) {
  if (fps!=24 && fps!=60) return false;
  const int frames=seconds*fps, perFrame=rate/fps, block=fps==60?200:500;
  if (revision.length()!=40 || !revision.containsOnly("0123456789abcdef")
      || out.exists() || !out.createDirectory()) return false;
  SkellAudioProcessor proc;
  proc.enableAllBuses();
  proc.setRateAndBufferSizeDetails(rate,block);
  proc.prepareToPlay(rate,block);
  if (proc.getTotalNumInputChannels()!=4 || proc.getLatencySamples()!=0) return false;
  proc.setTheme(1);
  std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
  if (!editor) return false;
  editor->setSize(550,450);
  pump(100);
  auto* amount=find<juce::Slider>(*editor,[](juce::Slider& s) {
    return s.isRotary() && s.getMinimum()==0. && s.getMaximum()==100. && s.getValue()==50.;
  });
  auto* parameter=proc.parameters.getParameter("amount");
  auto* raw=proc.parameters.getRawParameterValue("amount");
  auto* power=find<juce::TextButton>(*editor,[](juce::TextButton& b){return b.getButtonText()=="Power";});
  auto* listen=find<juce::TextButton>(*editor,[](juce::TextButton& b){return b.getButtonText()=="Listen";});
  auto* powerRaw=proc.parameters.getRawParameterValue("power");
  auto* listenRaw=proc.parameters.getRawParameterValue("listen");
  if (!amount || !parameter || !raw || !power || !listen || !powerRaw || !listenRaw) return false;
  if (powerRaw->load()<.5f || listenRaw->load()>.5f) return false;
  juce::StringArray events;
  events.add("frame,sample,control,value");
  Wav dry(out.getChildFile("dry.wav")), wet(out.getChildFile("processed.wav")), key(out.getChildFile("sidechain.wav"));
  if (!dry.header() || !wet.header() || !key.header()) return false;
  juce::StringArray csv;
  csv.add("frame,sample,parameter,normalized,actual,input_rms,output_rms,input_peak,output_peak");
  juce::AudioBuffer<float> buffer(4,block);
  juce::MidiBuffer midi;
  const auto tau=juce::MathConstants<double>::twoPi;
  for (int frame=0;frame<frames;++frame) {
    // Ramp through the actual GUI control, not a reconstructed dial.
    const auto requested=90.*juce::jlimit(0.,1.,(frame/double(fps)-1.)/4.);
    // Explicit real button events. Wait for JUCE's async click and verify
    // the attached host parameter before any samples for this frame.
    if (frame==8*fps || frame==10*fps || frame==12*fps || frame==14*fps) {
      const bool isPower=frame<12*fps;
      auto* button=isPower?power:listen;
      auto* value=isPower?powerRaw:listenRaw;
      const float expected=(frame==10*fps || frame==12*fps)?1.f:0.f;
      button->triggerClick();pump(60);
      if (std::abs(value->load()-expected)>.01f) return false;
      events.add(juce::String(frame)+","+juce::String(frame*perFrame)+","+
                 (isPower?"power":"listen")+","+juce::String(value->load(),1));
    }
    amount->setValue(requested,juce::sendNotificationSync);
    const float actual=raw->load(), normalized=parameter->getValue();
    if (std::abs(actual-amount->getValue())>.11 || std::abs(actual-normalized*100.f)>.11) return false;
    Meter a,b;
    for (int offset=0;offset<perFrame;offset+=block) {
      for (int i=0;i<block;++i) {
        const auto t=(frame*perFrame+offset+i)/double(rate);
        const double pulse=.35+.65*std::pow(.5+.5*std::sin(tau*2.*t),2.);
        const float main=(float)(.12*std::sin(tau*110.*t)+.13*std::sin(tau*880.*t)+.08*std::sin(tau*2640.*t));
        const float side=(float)(pulse*(.32*std::sin(tau*880.*t)+.12*std::sin(tau*2640.*t)));
        double v=0.;if (!dry.sample(main,v)) return false;a.add(v);
        if (!key.sample(side,v)) return false;
        buffer.setSample(0,i,main);buffer.setSample(1,i,main);
        buffer.setSample(2,i,side);buffer.setSample(3,i,side);
      }
      proc.processBlock(buffer,midi);
      for (int i=0;i<block;++i) { double v=0.; if (!wet.sample(buffer.getSample(0,i),v)) return false;b.add(v); }
    }
    // Let the actual editor timer draw the measured spectrum and meter.
    pump(fps==60?18:42);
    auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,2.f);
    if (image.isNull() || image.getWidth()!=1100 || image.getHeight()!=900) return false;
    juce::FileOutputStream png(out.getChildFile("frame_"+juce::String(frame).paddedLeft('0',6)+".png"));
    if (!png.openedOk() || !juce::PNGImageFormat().writeImageToStream(image,png)) return false;
    png.flush();if (png.getStatus().failed()) return false;
    csv.add(juce::String(frame)+","+juce::String(frame*perFrame)+",amount,"+
      juce::String(normalized,9)+","+juce::String(actual,9)+","+juce::String(a.rms(perFrame),9)+","+
      juce::String(b.rms(perFrame),9)+","+juce::String(a.peak,9)+","+juce::String(b.peak,9));
  }
  dry.stream.flush();wet.stream.flush();key.stream.flush();
  if (dry.stream.getStatus().failed() || wet.stream.getStatus().failed() || key.stream.getStatus().failed()
      || !proc.isSidechainActive() || !out.getChildFile("editor.csv").replaceWithText(csv.joinIntoString("\n")+"\n")
      || !out.getChildFile("controls.csv").replaceWithText(events.joinIntoString("\n")+"\n")) return false;
  const auto meta=juce::String("{\"schema\":\"skell.demo.editor.v1\",\"origin\":\"offline_juce_editor\","
    "\"product\":\"Skell Spacer\",\"parameter\":\"amount\",\"revision\":\"")+revision+
    "\",\"fps\":"+juce::String(fps)+",\"frame_count\":"+juce::String(frames)+",\"sample_rate\":48000,\"samples\":768000,"
    "\"width\":1100,\"height\":900,\"source_kind\":\"synthetic_test_signal\",\"latency_samples\":0,\"control_trace\":true}";
  return out.getChildFile("capture.json").replaceWithText(meta);
}
} // namespace spacer_demo
