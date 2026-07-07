#pragma once

#include <optional>

#include "PluginProcessor.h"

//==============================================================================
/*
    NumberBox

    A LinearBar slider used as a plain number box.

    Interactions:
      - click-drag           scrub the value (relative, no jumps)
      - double-click         reset to the box's default value
      - single click, pause  open the text editor to type a value
        (the short pause is what distinguishes a click from a double-click)

    The slider's internal value label is made mouse-transparent so the slider
    itself sees every click; otherwise the label would open its text editor on
    the first click and a double-click could never be detected.
*/
class NumberBox : public juce::Slider,
                  private juce::Timer
{
public:
    NumberBox()
    {
        setSliderStyle (juce::Slider::LinearBar);
        setTextBoxIsEditable (true);                // required for showTextBox()
        setSliderSnapsToMousePosition (false);      // dragging is relative
        setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);

        applyLabelTweaks();
    }

    void setResetValue (double newResetValue)       { resetValue = newResetValue; }

    void setBoxFont (juce::Font newFont)
    {
        boxFont = std::move (newFont);
        applyLabelTweaks();
    }

    void lookAndFeelChanged() override
    {
        juce::Slider::lookAndFeelChanged();         // (re)creates the value label
        applyLabelTweaks();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        stopTimer();
        juce::Slider::mouseDown (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp (e);

        if (e.getNumberOfClicks() == 1 && ! e.mouseWasDraggedSinceMouseDown() && isEnabled())
            startTimer (juce::MouseEvent::getDoubleClickTimeout());
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        stopTimer();
        setValue (resetValue, juce::sendNotificationSync);
    }

private:
    void timerCallback() override
    {
        stopTimer();
        showTextBox();
    }

    void applyLabelTweaks()
    {
        for (auto* child : getChildren())
        {
            if (auto* label = dynamic_cast<juce::Label*> (child))
            {
                // The slider handles all clicks; the label's own text editor
                // (a child of the label) must stay clickable while it's open.
                label->setInterceptsMouseClicks (false, true);

                if (boxFont.has_value())
                    label->setFont (*boxFont);
            }
        }
    }

    double resetValue = 0.0;
    std::optional<juce::Font> boxFont;
};

//==============================================================================
class HeadroomTrimEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit HeadroomTrimEditor (HeadroomTrimProcessor&);
    ~HeadroomTrimEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    HeadroomTrimProcessor& processor;

    NumberBox headroomBox, safetyBox, idBox;
    juce::Label headroomCaption, safetyCaption;   // small titles above the boxes
    juce::Label headroomUnit, safetyUnit;         // the "dB" labels
    juce::Label maxLabel, distanceLabel;          // live readouts
    juce::TextButton resetButton { "Reset" };
    juce::ToggleButton truePeakToggle   { "True Peak" };
    juce::ToggleButton noStartlesToggle { "No Startles" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> headroomAttachment, safetyAttachment, idAttachment;
    std::unique_ptr<ButtonAttachment> truePeakAttachment, noStartlesAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadroomTrimEditor)
};
