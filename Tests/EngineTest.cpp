// EngineTest -- verifies the "No Startles" guarantee numerically.
//
// Simulates the exact per-sample pipeline the plugin runs in No Startles mode
// (ratchet -> required gain -> LookaheadGainEngine) against a signal full of
// sudden random loudness bursts, and asserts that no output sample ever
// exceeds the Headroom ceiling. Also reports the true-peak detector's group
// delay, which sets the lookahead margin.

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstdio>
#include <random>

#include "../Source/LookaheadGainEngine.h"

// Same formula as HeadroomTrimProcessor::computeRequiredGain.
static float requiredGain (float maxLinear, float headroomDb, float safetyDb)
{
    const float maxDb = juce::Decibels::gainToDecibels (maxLinear, -120.0f);
    return juce::Decibels::decibelsToGain (juce::jmin (headroomDb - maxDb, safetyDb));
}

int main()
{
    // --- 1) True-peak detector margin (base-rate samples) -------------------
    juce::dsp::Oversampling<float> os (2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        false, true);
    os.initProcessing (512);

    const float osLatency = os.getLatencyInSamples();
    std::printf ("true-peak detector group delay: %.2f base-rate samples\n", osLatency);

    // --- 2) Ceiling test: 30 s of random amplitude bursts --------------------
    const double fs     = 48000.0;
    const int    window = juce::jmax (16, juce::roundToInt (0.003 * fs)); // 3 ms
    const int    margin = (int) std::ceil (osLatency);

    LookaheadGainEngine engine;
    engine.prepare (2, window, margin);

    std::printf ("lookahead: window %d + margin %d = %d samples (%.2f ms at %.0f Hz)\n",
                 window, margin, engine.getLatencySamples(),
                 1000.0 * engine.getLatencySamples() / fs, fs);

    const float headroomDb = -8.0f;
    const float safetyDb   = 0.0f;
    const float ceiling    = juce::Decibels::decibelsToGain (headroomDb);

    std::mt19937 rng (7);
    std::uniform_real_distribution<float> amp (0.05f, 1.2f);

    float  maxLin  = 0.0f;
    float  r       = requiredGain (maxLin, headroomDb, safetyDb);
    float  outPeak = 0.0f;
    double phase   = 0.0;

    float currentAmp = 0.05f;
    int   samplesToNextBurst = 0;

    const long total = (long) (fs * 30.0);

    for (long n = 0; n < total; ++n)
    {
        // Instant amplitude jumps at random times: worst case for overshoot.
        if (samplesToNextBurst-- <= 0)
        {
            currentAmp = amp (rng);
            samplesToNextBurst = 2000 + (int) (rng() % 20000u);
        }

        phase += 2.0 * juce::MathConstants<double>::pi * 997.0 / fs;
        const float x = currentAmp * (float) std::sin (phase);

        float frame[2] = { x, 0.7f * x };

        // Exact plugin order: measure -> save Max -> set gain -> then output.
        const float inPeak = juce::jmax (std::abs (frame[0]), std::abs (frame[1]));

        if (inPeak > maxLin)
        {
            maxLin = inPeak;
            r = requiredGain (maxLin, headroomDb, safetyDb);
        }

        engine.processFrame (r, frame, 2);

        outPeak = juce::jmax (outPeak, std::abs (frame[0]), std::abs (frame[1]));
    }

    std::printf ("ceiling %.6f | output peak %.6f | difference %+.4f dB\n",
                 ceiling, outPeak,
                 juce::Decibels::gainToDecibels (outPeak / ceiling));

    const bool ceilingPass = outPeak <= ceiling * 1.000001f;
    std::printf ("%s: burst test (%ld samples)\n", ceilingPass ? "PASS" : "FAIL", total);

    // --- 3) Safety boost check: quiet signal, Safety +6 dB -------------------
    LookaheadGainEngine engine2;
    engine2.prepare (2, window, margin);

    const float safety2 = 6.0f;
    float maxLin2 = 0.0f, r2 = requiredGain (maxLin2, headroomDb, safety2);
    float outPeak2 = 0.0f;
    phase = 0.0;

    for (long n = 0; n < (long) fs; ++n)
    {
        phase += 2.0 * juce::MathConstants<double>::pi * 997.0 / fs;
        const float x = 0.1f * (float) std::sin (phase);
        float frame[2] = { x, x };

        const float inPeak = std::abs (x);
        if (inPeak > maxLin2)
        {
            maxLin2 = inPeak;
            r2 = requiredGain (maxLin2, headroomDb, safety2);
        }

        engine2.processFrame (r2, frame, 2);
        outPeak2 = juce::jmax (outPeak2, std::abs (frame[0]));
    }

    // Expected: 0.1 boosted by exactly +6 dB (Safety caps the +12 dB distance).
    const float expected = 0.1f * juce::Decibels::decibelsToGain (safety2);
    const bool  boostPass = std::abs (outPeak2 - expected) < 0.001f && outPeak2 <= ceiling;

    std::printf ("safety boost: expected %.4f, got %.4f -> %s\n",
                 expected, outPeak2, boostPass ? "PASS" : "FAIL");

    return (ceilingPass && boostPass) ? 0 : 1;
}
