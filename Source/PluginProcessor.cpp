#include <SkellCore/Core.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
SkellAudioProcessor::SkellAudioProcessor()
 : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
   parameters(*this,nullptr,"PARAMETERS",createLayout()) {}
juce::AudioProcessorValueTreeState::ParameterLayout SkellAudioProcessor::createLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"gain",1},"Gain",juce::NormalisableRange<float>(0.0f,2.0f,0.001f),1.0f));
  return layout;
}
bool SkellAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
  auto input=l.getMainInputChannelSet();
  return (input==juce::AudioChannelSet::mono() || input==juce::AudioChannelSet::stereo()) && input==l.getMainOutputChannelSet();
}
void SkellAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
  juce::ScopedNoDenormals guard;
  auto gain=parameters.getRawParameterValue("gain")->load();
  buffer.applyGain(gain);
}
juce::AudioProcessorEditor* SkellAudioProcessor::createEditor() { return new SkellAudioProcessorEditor(*this); }
void SkellAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
  if(auto xml=parameters.copyState().createXml()) copyXmlToBinary(*xml,dest);
}
void SkellAudioProcessor::setStateInformation(const void* data,int size) {
  if(auto xml=getXmlFromBinary(data,size))
    if(xml->hasTagName(parameters.state.getType())) parameters.replaceState(juce::ValueTree::fromXml(*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkellAudioProcessor(); }
