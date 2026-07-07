#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "LookaheadGainEngine.h"

//==============================================================================
/*
    Headroom v2

    Tracks the all-time input peak ("Max") and applies an output gain of
        gain (dB) = min (Headroom - Max, Safety)
    so the loudest peak ever seen lands exactly at the Headroom target,
    with upward gain capped by Safety.

    Two independent options:
      - "No Startles" (default ON): measure -> update Max -> set gain -> THEN
        output, via a short lookahead delay, so no sample can ever leave the
        plugin above the Headroom ceiling. Adds a few ms of reported latency.
        OFF = the original v1 reactive behaviour (zero latency, gain follows
        peaks with a 20 ms glide, so a brand-new peak can briefly overshoot).
      - "True Peak" (default OFF): the ratchet measures 4x-oversampled
        inter-sample peaks instead of sample peaks.
*/
class HeadroomTrimProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    HeadroomTrimProcessor();
    ~HeadroomTrimProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // keep the double-precision overload visible

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    //==============================================================================
    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Peak tracking. The GUI requests a reset; the audio thread consumes it at
    // the start of the next block, so the ratchet has a single writer.
    void resetMax() noexcept                               { resetRequest.store (true); }
    float getMaxLinear() const noexcept                    { return maxPeakLinear.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static float computeRequiredGain (float maxLinear, float headroomDb, float safetyDb);

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void updateHostLatency();

    std::atomic<float>* headroomParam   = nullptr;
    std::atomic<float>* safetyParam     = nullptr;
    std::atomic<float>* noStartlesParam = nullptr;
    std::atomic<float>* truePeakParam   = nullptr;

    std::atomic<float> maxPeakLinear { 0.0f };        // all-time peak, linear amplitude
    std::atomic<bool>  resetRequest  { false };

    // Reactive (v1) path
    juce::SmoothedValue<float> gainSmoothed { 1.0f }; // de-clicks gain changes (adds no latency)

    // No Startles path
    LookaheadGainEngine engine;
    bool wasNoStartles = true;

    // True-peak detector (4x oversampling, measurement only)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::vector<float> peakEstimate;                  // per-sample inter-sample peak

    // Audio-thread cache so dB<->linear maths only runs when something changed
    float lastMax = -1.0f, lastHeadroom = 0.0f, lastSafety = 0.0f, cachedR = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadroomTrimProcessor)
};
