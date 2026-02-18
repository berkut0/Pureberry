# USB Audio Stability History (with I2C Enabled and Disabled)

## 1) Purpose and scope
This note is a historical context document.
It tracks how USB-audio glitch investigation evolved over time and what is currently known.

This document intentionally avoids temporary build artifact paths and one-off flashing instructions.

## 2) System context and hard constraints
- MCU: RP2350.
- core1: Heavy DSP + I2S output hot path.
- core0: TinyUSB service, USB FIFO drain, I2C/UI/peripheral tasks.
- USB input flow: host -> TinyUSB FIFO (core0) -> shared ring -> core1 pop -> Heavy input.

Important implication:
- USB-audio quality depends on both core0 service cadence and core1 continuity handling.

## 3) Reproduction context used during investigation
Typical reproducible setup used across iterations:
- Simple pass-through style patch (`adc_to_dac` family, including knob variant).
- External sine source from host (for example 90 Hz) to reveal phase drift and transient clicks.
- OLED waveform + menu activity used as stressor.

## 4) Symptom pattern that persisted across iterations
Observed repeatedly:
- A repeating instability wave:
  - short degradation phase
  - partial recovery
  - short stable window
  - repeat
- Short spike/crackle events around ~5 ms spacing in some runs.
- Short event duration around ~400 us in some runs.
- Phase-slip/phase-drift wave around ~32-40 ms in some runs.
- Entering `System` menu usually increases artifact probability/severity.

Signal character notes:
- Some transients trend toward zero crossing but are not strict hard-zero cuts.
- OLED visualization shows phase motion well, but may miss sub-millisecond spikes due to decimation/frame limits.

## 5) Timeline of key implementation milestones (commit-backed)

### 5.1 USB-audio path introduction
- `4adb3a7` Add USB Audio (UAC2 speaker) input path.
- `8203986` Tune I2S buffering and add build flag support.

Outcome:
- Functional USB input path established.
- Stability issues remained under mixed system load.

### 5.2 Early USB input fix before full i2c-dma merge
- `55827f8` Restore USB input path in `audio_runtime`.

Outcome:
- Recovered expected input behavior for that stage.
- Did not eliminate the broader glitch class.

### 5.3 I2C architecture migration to unified non-blocking transport
- `2e7d432` Unify I2C bus API and isolate drivers from transport internals.
- `55a674f` Unify non-blocking transport and remove DMA feature flag.
- `65b1ee1` Ensure i2c-dma callback runs only after transaction completion.
- `dab4085` Harden async I2C contract and simplify DMA engine.
- `05fc089` Rewrite I2C guide and simplify runtime recovery.
- `e974fe7` Fail-fast for blocking I2C calls under async DMA contention.
- `9ec23c1` Gate MPR121 runtime reads by patch routes.

Outcome:
- Architecture became cleaner and more consistent.
- Transport behavior improved and became less blocking by design.
- Audio artifacts changed shape/timing but did not disappear.

### 5.4 USB deterministic and bounded-control work
- `1df9940` Enforce deterministic 48 kHz path and bound core1 control-drain work.

Outcome:
- Reduced some nondeterminism.
- Glitches remained under stress, especially with active UI/I2C.

### 5.5 Current checkpoint with broader mitigation bundle
- `79ad9ab` USB/audio/UI/I2C mitigation checkpoint + historical capture.

Included direction (high level):
- bounded USB task pull budget per service call
- tighter cross-core streaming state semantics
- refined feedback shaping in USB path
- core1 short-read continuity handling
- UI/OLED backpressure and reduced per-frame work
- MPR121 burst level reads

Outcome:
- Instability wave became shorter/faster in many observations.
- Core issue class remained.

## 6) Critical A/B conclusion that invalidated a major hypothesis
Tested hypothesis:
- "It was better before full i2c-dma merge."

Result:
- Not supported.
- Pre-i2c-dma baseline (`55827f8`) and current branch share the same glitch class.
- Difference is mostly temporal character (wave length/density), not root disappearance.

Meaning:
- Reverting i2c-dma is not a root-cause fix by itself.

## 7) Current status matrix (what to remember)
- USB audio + OLED + I2C enabled:
  - Works, but glitches are still reproducible.
- USB audio with less I2C/UI pressure:
  - Better, but still not 100% clean in all long runs.
- Therefore:
  - Problem is not binary "I2C on = broken / I2C off = perfect".
  - It is a stability margin problem affected by mixed-load timing and clock-domain behavior.

## 8) Most credible root-cause cluster (historical consensus)
Combined factors are more likely than a single bug:
- USB feedback servo dynamics between host clock and local audio clock.
- micro-gap / short-read behavior at ring boundary.
- core0 service jitter under concurrent USB + UI + I2C work.

## 9) What was ruled out (or weakened)
- "I2C DMA refactor alone introduced the issue": weakened by A/B.
- "Going back to old branch state solves it": not supported.

## 10) Resume checklist for future work
When resuming this track:
1. Re-run the simple sine-based reproduction first.
2. Check whether current run is spike-dominant, phase-wave-dominant, or mixed.
3. Validate behavior in both modes:
   - OLED/I2C active
   - OLED/I2C minimized
4. Keep changes minimal and measurable.
5. Preserve architecture constraints (core roles, RT safety, no layer leakage).

## 11) Note ownership
This file is intentionally historical.
Keep it updated when new evidence changes previous conclusions.
Do not turn it into a temporary build log.
