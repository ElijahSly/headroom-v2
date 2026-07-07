#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

/*
    LookaheadGainEngine

    Delays the audio by (window + margin) samples and applies, to each outgoing
    sample, the moving average over the last `window` values of the per-sample
    required gain.

    Why this guarantees the ceiling is never exceeded:
    the required gain in this plugin only ever ratchets DOWN while settings are
    held (Max only ratchets up). The sample leaving the delay line entered
    (window + margin) samples ago, so its own required gain was already known
    at least `window` samples ago. Every value inside the averaging window is
    newer than that, therefore equal or LOWER, so the average applied to the
    outgoing sample can never exceed the gain that sample needs. The averaging
    turns each gain step into a linear ramp of `window` samples (click-free),
    and the ramp always finishes before the peak that caused it reaches the
    output. `margin` gives the same headroom to a detector that reports peaks
    late (the true-peak oversampler's group delay).
*/
class LookaheadGainEngine
{
public:
    void prepare (int numChannels, int windowSamples, int marginSamples)
    {
        window    = std::max (1, windowSamples);
        invWindow = 1.0 / (double) window;
        delayLen  = window + std::max (0, marginSamples);

        gainHistory.assign ((size_t) window, 1.0f);
        delay.assign ((size_t) std::max (1, numChannels),
                      std::vector<float> ((size_t) delayLen, 0.0f));
        resetState (1.0f);
    }

    // Zero the delay line and pin the gain average to a known value
    // (used when the mode is switched on, so there is no ramp from unity).
    void resetState (float seedGain) noexcept
    {
        std::fill (gainHistory.begin(), gainHistory.end(), seedGain);
        runningSum = (double) seedGain * (double) window;

        for (auto& line : delay)
            std::fill (line.begin(), line.end(), 0.0f);

        gainIndex = 0;
        writeIndex = 0;
        currentGain = seedGain;
    }

    int getLatencySamples() const noexcept   { return delayLen; }
    float getCurrentGain() const noexcept    { return currentGain; }

    // requiredGain must already include THIS frame's peak in the ratchet.
    // frame[] holds one sample per channel and is replaced with the delayed,
    // gain-scaled output.
    inline void processFrame (float requiredGain, float* frame, int numChannels) noexcept
    {
        runningSum += (double) requiredGain - (double) gainHistory[(size_t) gainIndex];
        gainHistory[(size_t) gainIndex] = requiredGain;
        if (++gainIndex == window)
            gainIndex = 0;

        const float g = (float) (runningSum * invWindow);
        currentGain = g;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& line = delay[(size_t) ch];
            const float delayed = line[(size_t) writeIndex];
            line[(size_t) writeIndex] = frame[ch];
            frame[ch] = delayed * g;
        }

        if (++writeIndex == delayLen)
            writeIndex = 0;
    }

private:
    std::vector<std::vector<float>> delay;
    std::vector<float> gainHistory;
    double runningSum = 0.0, invWindow = 1.0;
    int window = 1, delayLen = 0, gainIndex = 0, writeIndex = 0;
    float currentGain = 1.0f;
};
