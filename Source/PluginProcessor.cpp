#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HeadroomTrimProcessor::HeadroomTrimProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    headroomParam   = apvts.getRawParameterValue ("headroom");
    safetyParam     = apvts.getRawParameterValue ("safety");
    noStartlesParam = apvts.getRawParameterValue ("nostartles");
    truePeakParam   = apvts.getRawParameterValue ("truepeak");

    // Reported latency depends on the No Startles toggle.
    apvts.addParameterListener ("nostartles", this);
}

HeadroomTrimProcessor::~HeadroomTrimProcessor()
{
    apvts.removeParameterListener ("nostartles", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
HeadroomTrimProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    // Headroom: default -8.00 dB, range -60.00 .. 0.00, typed input snaps to
    // 0.01 dB steps (two decimals).
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "headroom", 1 },
        "Headroom",
        NormalisableRange<float> (-60.0f, 0.0f, 0.01f),
        -8.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Safety: default 0.00 dB, range 0.00 .. 12.00, snaps to 0.01 dB steps.
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "safety", 1 },
        "Safety",
        NormalisableRange<float> (0.0f, 12.0f, 0.01f),
        0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Instance ID: cosmetic label 1-9, integers only, not automatable.
    params.push_back (std::make_unique<AudioParameterInt> (
        ParameterID { "instance", 1 },
        "Instance ID",
        1, 9, 1,
        AudioParameterIntAttributes().withAutomatable (false)));

    // No Startles: lookahead mode, ON by default. Not automatable because
    // toggling it changes the plugin's reported latency mid-flight.
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "nostartles", 1 },
        "No Startles",
        true,
        AudioParameterBoolAttributes().withAutomatable (false)));

    // True Peak: measure 4x-oversampled inter-sample peaks, OFF by default.
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "truepeak", 1 },
        "True Peak",
        false,
        AudioParameterBoolAttributes().withAutomatable (false)));

    return { params.begin(), params.end() };
}

//==============================================================================
float HeadroomTrimProcessor::computeRequiredGain (float maxLinear, float headroomDb, float safetyDb)
{
    // Distance = Headroom - Max;  applied gain (dB) = min (Distance, Safety).
    const float maxDb = juce::Decibels::gainToDecibels (maxLinear, -120.0f);
    return juce::Decibels::decibelsToGain (juce::jmin (headroomDb - maxDb, safetyDb));
}

void HeadroomTrimProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Lookahead averaging window: 3 ms (the gain glide time in No Startles mode).
    const int windowSamples = juce::jmax (16, juce::roundToInt (0.003 * sampleRate));
    const int numChannels   = juce::jmax (1, getTotalNumInputChannels());

    // True-peak detector: 4x oversampling, measurement only (audio path never
    // runs through it). Its group delay is folded into the lookahead margin so
    // toggling True Peak does not change the reported latency.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, 2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        false, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    oversampler->reset();

    const int margin = (int) std::ceil (oversampler->getLatencyInSamples());

    engine.prepare (numChannels, windowSamples, margin);
    peakEstimate.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);

    // Seed everything at the gain implied by the current state, so reopening a
    // saved set does not ramp up from unity.
    lastHeadroom = headroomParam->load();
    lastSafety   = safetyParam->load();
    lastMax      = maxPeakLinear.load();
    cachedR      = computeRequiredGain (lastMax, lastHeadroom, lastSafety);

    engine.resetState (cachedR);

    // 20 ms ramp for the reactive path: inaudible, click-free, zero added latency.
    gainSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue (cachedR);

    wasNoStartles = noStartlesParam->load() > 0.5f;
    updateHostLatency();
}

bool HeadroomTrimProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void HeadroomTrimProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "nostartles")
        updateHostLatency();
}

void HeadroomTrimProcessor::updateHostLatency()
{
    const bool ns = noStartlesParam != nullptr && noStartlesParam->load() > 0.5f;
    setLatencySamples (ns ? engine.getLatencySamples() : 0);
}

//==============================================================================
void HeadroomTrimProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numSamples == 0)
        return;

    // Consume a pending Reset. The audio thread is the only writer of the
    // ratchet, so a reset can never be clobbered by a concurrent peak update.
    if (resetRequest.exchange (false))
        maxPeakLinear.store (0.0f, std::memory_order_relaxed);

    const bool  noStartles = noStartlesParam->load() > 0.5f;
    const bool  truePeak   = truePeakParam->load()  > 0.5f;
    const float headroomDb = headroomParam->load();
    const float safetyDb   = safetyParam->load();

    // ---- True Peak: build a per-sample inter-sample-peak estimate -----------
    const float* peakPerSample = nullptr;

    if (truePeak && oversampler != nullptr)
    {
        if ((int) peakEstimate.size() < numSamples)          // defensive; hosts
            peakEstimate.resize ((size_t) numSamples);       // shouldn't do this

        juce::dsp::AudioBlock<float> block (buffer);
        auto os = oversampler->processSamplesUp (block);

        const int factor = juce::jmax (1, (int) (os.getNumSamples() / (size_t) numSamples));

        for (int n = 0; n < numSamples; ++n)
        {
            float p = 0.0f;

            for (size_t ch = 0; ch < os.getNumChannels(); ++ch)
            {
                const float* d = os.getChannelPointer (ch);

                for (int k = 0; k < factor; ++k)
                    p = juce::jmax (p, std::abs (d[(size_t) (n * factor + k)]));
            }

            peakEstimate[(size_t) n] = p;
        }

        peakPerSample = peakEstimate.data();
    }

    // ---- Mode transitions ----------------------------------------------------
    if (noStartles != wasNoStartles)
    {
        wasNoStartles = noStartles;

        if (noStartles)
        {
            // Entering lookahead mode: clean delay line, gain pinned at the
            // value implied by the current state (no ramp from unity).
            lastMax      = maxPeakLinear.load (std::memory_order_relaxed);
            lastHeadroom = headroomDb;
            lastSafety   = safetyDb;
            cachedR      = computeRequiredGain (lastMax, headroomDb, safetyDb);
            engine.resetState (cachedR);
        }
        else
        {
            // Back to the reactive path: hand the smoother today's gain.
            gainSmoothed.setCurrentAndTargetValue (cachedR);
        }
    }

    // Refresh the cached required gain if the settings or Max changed.
    float currentMax = maxPeakLinear.load (std::memory_order_relaxed);

    if (! juce::exactlyEqual (currentMax, lastMax)
        || ! juce::exactlyEqual (headroomDb, lastHeadroom)
        || ! juce::exactlyEqual (safetyDb, lastSafety))
    {
        lastMax      = currentMax;
        lastHeadroom = headroomDb;
        lastSafety   = safetyDb;
        cachedR      = computeRequiredGain (currentMax, headroomDb, safetyDb);
    }

    // ==== Path A: No Startles (measure -> save Max -> set gain -> output) =====
    if (noStartles)
    {
        float* chData[2] = { nullptr, nullptr };            // layout is mono/stereo only
        const int chCount = juce::jmin (numChannels, 2);

        for (int ch = 0; ch < chCount; ++ch)
            chData[ch] = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            // 1) Measure this sample (or its inter-sample peak estimate).
            float inPeak;

            if (peakPerSample != nullptr)
            {
                inPeak = peakPerSample[n];
            }
            else
            {
                inPeak = 0.0f;
                for (int ch = 0; ch < chCount; ++ch)
                    inPeak = juce::jmax (inPeak, std::abs (chData[ch][n]));
            }

            // 2) Save Max (ratchet) and 3) update the gain it requires.
            if (inPeak > currentMax)
            {
                currentMax = inPeak;
                lastMax    = currentMax;
                cachedR    = computeRequiredGain (currentMax, headroomDb, safetyDb);
            }

            // 4) THEN output: the engine delays the audio and applies a gain
            //    that already accounts for this sample.
            float frame[2] = { 0.0f, 0.0f };

            for (int ch = 0; ch < chCount; ++ch)
                frame[ch] = chData[ch][n];

            engine.processFrame (cachedR, frame, chCount);

            for (int ch = 0; ch < chCount; ++ch)
                chData[ch][n] = frame[ch];
        }

        maxPeakLinear.store (currentMax, std::memory_order_relaxed);
        return;
    }

    // ==== Path B: reactive, exactly the v1 behaviour ===========================
    // 1) Update the all-time peak (Max) from the incoming audio.
    float blockPeak = 0.0f;

    if (peakPerSample != nullptr)
    {
        for (int n = 0; n < numSamples; ++n)
            blockPeak = juce::jmax (blockPeak, peakPerSample[n]);
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
            blockPeak = juce::jmax (blockPeak, buffer.getMagnitude (ch, 0, numSamples));
    }

    if (blockPeak > currentMax)
    {
        currentMax = blockPeak;
        maxPeakLinear.store (currentMax, std::memory_order_relaxed);

        lastMax = currentMax;
        cachedR = computeRequiredGain (currentMax, headroomDb, safetyDb);
    }

    // 2) Glide toward the required gain, 3) apply to the output.
    gainSmoothed.setTargetValue (cachedR);

    if (gainSmoothed.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gainSmoothed.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }
    else
    {
        buffer.applyGain (gainSmoothed.getTargetValue());
    }
}

//==============================================================================
// The stored Max is saved inside the Live set alongside the parameters, so a
// saved project reopens with the same gain instead of jumping back up.
void HeadroomTrimProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("maxPeakLinear", (double) maxPeakLinear.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HeadroomTrimProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto newState = juce::ValueTree::fromXml (*xml);
            maxPeakLinear.store ((float) (double) newState.getProperty ("maxPeakLinear", 0.0));
            apvts.replaceState (newState);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* HeadroomTrimProcessor::createEditor()
{
    return new HeadroomTrimEditor (*this);
}

// This creates the plugin instance for the host.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HeadroomTrimProcessor();
}
