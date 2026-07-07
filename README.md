# Headroom v2

A VST3 utility for Ableton Live. It tracks the all-time input peak ("Max") and
applies an output gain of

    gain (dB) = min (Headroom - Max, Safety)

so the loudest peak ever seen lands exactly at the Headroom target. Max only
ratchets upward until you press Reset. The stored Max is saved inside the Live
set, so a saved project reopens with the same gain.

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
