#include "PluginEditor.h"

//==============================================================================
HeadroomTrimEditor::HeadroomTrimEditor (HeadroomTrimProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    addAndMakeVisible (headroomBox);
    addAndMakeVisible (safetyBox);
    addAndMakeVisible (idBox);

    // Attach to parameters: this gives each box its range, its typed-input
    // snapping (0.01 steps for the dB boxes, whole numbers 1-9 for the ID box),
    // and two-decimal display, all driven by the parameter definitions.
    headroomAttachment = std::make_unique<SliderAttachment> (processor.apvts, "headroom", headroomBox);
    safetyAttachment   = std::make_unique<SliderAttachment> (processor.apvts, "safety",   safetyBox);
    idAttachment       = std::make_unique<SliderAttachment> (processor.apvts, "instance", idBox);

    // Double-click resets (set after the attachments so nothing overrides them).
    headroomBox.setResetValue (-8.0);
    safetyBox  .setResetValue (0.0);
    idBox      .setResetValue (1.0);

    // ID digit: twice the default 15 px label height, and bold.
    idBox.setBoxFont (juce::Font (juce::FontOptions (30.0f)).boldened());

    auto initLabel = [this] (juce::Label& label, const juce::String& text, float fontSize)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (fontSize));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    };

    initLabel (headroomCaption, "Headroom", 12.0f);
    initLabel (safetyCaption,   "Safety",   12.0f);
    initLabel (headroomUnit,    "dB",       15.0f);
    initLabel (safetyUnit,      "dB",       15.0f);
    initLabel (maxLabel,        "Max: -inf dB",  15.0f);
    initLabel (distanceLabel,   "Distance: --",  15.0f);

    resetButton.onClick = [this] { processor.resetMax(); };
    addAndMakeVisible (resetButton);

    addAndMakeVisible (truePeakToggle);
    addAndMakeVisible (noStartlesToggle);
    truePeakAttachment   = std::make_unique<ButtonAttachment> (processor.apvts, "truepeak",   truePeakToggle);
    noStartlesAttachment = std::make_unique<ButtonAttachment> (processor.apvts, "nostartles", noStartlesToggle);

    setSize (330, 148);

    startTimerHz (30);
    timerCallback(); // show correct values immediately on open
}

//==============================================================================
void HeadroomTrimEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void HeadroomTrimEditor::resized()
{
    const int margin   = 14;
    const int boxW     = 96;
    const int boxH     = 26;
    const int unitW    = 26;
    const int rightCol = 180;   // x position of the Safety column

    headroomCaption.setBounds (margin,   8, boxW, 14);
    safetyCaption  .setBounds (rightCol, 8, boxW, 14);

    headroomBox .setBounds (margin,              24, boxW, boxH);
    headroomUnit.setBounds (margin + boxW + 5,   24, unitW, boxH);

    safetyBox .setBounds (rightCol,            24, boxW, boxH);
    safetyUnit.setBounds (rightCol + boxW + 5, 24, unitW, boxH);

    // Readouts under the Headroom box
    maxLabel     .setBounds (margin, 62, 155, 18);
    distanceLabel.setBounds (margin, 86, 155, 18);

    // Under the Safety box: instance ID (narrower, taller) beside the Reset button
    idBox      .setBounds (rightCol,      62, 40, 42);
    resetButton.setBounds (rightCol + 50, 69, 66, 28);

    // Bottom row: True Peak below the Distance readout, No Startles below ID/Reset
    truePeakToggle  .setBounds (margin,   112, 110, 22);
    noStartlesToggle.setBounds (rightCol, 112, 120, 22);
}

//==============================================================================
void HeadroomTrimEditor::timerCallback()
{
    const float maxLinear  = processor.getMaxLinear();
    const float headroomDb = processor.apvts.getRawParameterValue ("headroom")->load();

    if (maxLinear < 1.0e-6f) // below -120 dB: nothing measured yet
    {
        maxLabel.setText ("Max: -inf dB", juce::dontSendNotification);
        distanceLabel.setText ("Distance: --", juce::dontSendNotification);
        return;
    }

    const float maxDb = juce::Decibels::gainToDecibels (maxLinear);

    maxLabel.setText ("Max: " + juce::String (maxDb, 2) + " dB",
                      juce::dontSendNotification);
    distanceLabel.setText ("Distance: " + juce::String (headroomDb - maxDb, 2) + " dB",
                           juce::dontSendNotification);
}
