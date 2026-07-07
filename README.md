# Headroom v2

A VST3 utility for Ableton Live. It tracks the all-time input peak ("Max") and
applies an output gain of

    gain (dB) = min (Headroom - Max, Safety)

so the loudest peak ever seen lands exactly at the Headroom target. Max only
ratchets upward until you press Reset. The stored Max is saved inside the Live
set, so a saved project reopens with the same gain.

v2 uses a new plugin ID, so it installs alongside v1 — existing sets that use
Headroom Trim keep loading v1 untouched.

## Controls

- **Headroom** (-60.00 to 0.00 dB, default -8.00): the target ceiling.
- **Safety** (0.00 to 12.00 dB, default 0.00): caps how much upward gain the
  plugin may apply when the signal sits below the target.
- **1-9 box**: cosmetic instance ID, big bold digit.
- **Reset**: forgets the stored Max.
- **True Peak** (default off): the ratchet measures 4x-oversampled
  inter-sample peaks instead of sample peaks. In reactive mode this adds CPU
  and a sub-millisecond detection lag but no audio latency; in No Startles
  mode its detector delay is already folded into the reported latency.
- **No Startles** (default on): measure, save Max, set the gain, *then*
  output. No sample can ever leave the plugin above the Headroom ceiling.
  This requires a short lookahead delay (3 ms averaging window plus the
  true-peak detector margin; 182 samples = 3.79 ms at 48 kHz), which is
  reported to Live so plugin delay compensation keeps everything in sync.
  Unchecked, the plugin behaves exactly like v1: zero latency, with the gain
  gliding to a new peak over about 20 ms (so a brand-new peak can briefly
  overshoot).

## Number box interactions

- Click-drag scrubs the value (relative, no jumps).
- Double-click resets to the default (-8.00 / 0.00 / 1).
- Single click, then a short pause (~0.4 s), opens the box for typing. The
  pause is what distinguishes a click from a double-click.

## Building on Windows

Requires Visual Studio 2022 (Community is fine, with the "Desktop development
with C++" workload) and CMake 3.22+. JUCE downloads automatically at configure
time.

    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release

Copy `build\HeadroomV2_artefacts\Release\VST3\Headroom v2.vst3` to
`C:\Program Files\Common Files\VST3\`, then rescan plugins in Live.

On a Mac the same source builds with Xcode + CMake (optionally add `AU` to
`FORMATS` in CMakeLists.txt and `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`).

## Building for macOS without a Mac

The included `.github/workflows/build-macos.yml` builds a universal
(Apple silicon + Intel) VST3 on GitHub's free macOS runners. Push this project
to a GitHub repository, open the Actions tab, and download the
`Headroom-v2-macOS` artifact when the run finishes. Unzip the inner
`Headroom-v2-macOS.zip` on the destination Mac (not on Windows -- the zip
preserves Mac file permissions), move `Headroom v2.vst3` into
`~/Library/Audio/Plug-Ins/VST3/`, then clear the download quarantine:

    xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Headroom v2.vst3"

and rescan plugins in Live.

## Engine test

An optional console test simulates the No Startles pipeline against 30 seconds
of random instant loudness bursts and asserts that no output sample exceeds
the Headroom ceiling:

    cmake -B build -DHEADROOM_BUILD_TESTS=ON
    cmake --build build --target EngineTest
    ./build/EngineTest_artefacts/Release/EngineTest

## Why No Startles needs latency

A zero-latency version that never exceeds the ceiling does exist: measure each
sample and snap the gain down within that same sample. But a many-dB gain snap
inside a single sample is itself an audible click at every new peak — it
trades the loudness startle for a transient artifact. To be both click-free
and ceiling-true, the gain must finish moving *before* the peak reaches the
output, which means buffering the audio briefly (lookahead).

The implementation delays the audio by (window + margin) samples and applies,
to each outgoing sample, the moving average of the per-sample required gain
over the last `window` samples. Because the required gain only ratchets
downward while settings are held, every value in that average is at or below
the gain the outgoing sample needs, so the ceiling can never be exceeded —
and each gain step becomes a smooth 3 ms linear ramp that always completes
before the peak that caused it emerges. (Moving the Headroom or Safety
controls themselves glides over the same 3 ms.)
