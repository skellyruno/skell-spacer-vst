#include <SkellCore/UI.h>
#include "PluginEditor.h"
SkellAudioProcessorEditor::SkellAudioProcessorEditor(SkellAudioProcessor& p)
 : AudioProcessorEditor(&p),gainAttachment(p.parameters,"gain",gain) {
  setSize(520,340);
  title.setText("Skell Quickstart",juce::dontSendNotification);
  title.setFont(juce::Font(juce::FontOptions(26.0f).withStyle("Bold")));
  title.setJustificationType(juce::Justification::centred);
  gain.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  gain.setTextBoxStyle(juce::Slider::TextBoxBelow,false,90,25);
  addAndMakeVisible(title);addAndMakeVisible(gain);
}
void SkellAudioProcessorEditor::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xff16191e));
  g.setColour(juce::Colour(0xff323a48));
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(12),12.0f,2.0f);
  g.setColour(juce::Colour(0xffb0c9ff));
  g.drawText("GAIN",getWidth()/2-60,245,120,24,juce::Justification::centred);
}
void SkellAudioProcessorEditor::resized() {
  title.setBounds(40,34,getWidth()-80,55);
  gain.setBounds(getWidth()/2-75,100,150,145);
}
