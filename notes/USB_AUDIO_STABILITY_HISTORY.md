# USB Audio Glitch A/B Checkpoint

## Context
This note captures the direct A/B comparison requested for the hypothesis:
"audio was better before the full i2c-dma integration".

The comparison used the same patch and flags:
- Patch: `pd-patches/adc_to_dac_knob1.pd`
- Build flags: `--usb-audio --mpr121 --oled`

## Baseline Builds
- Pre-i2c-dma baseline commit: `55827f8` (`fix(audio): restore USB input path in audio runtime`)
- Current working checkpoint: `usb_audio` branch with latest local runtime/UI/USB changes

Built artifacts for A/B flashing:
- `build/compare/adc_to_dac_knob1.pre_i2c_dma_55827f8.uf2`
- `build/compare/adc_to_dac_knob1.current_usb_audio_dirty.uf2`

## Observations (hardware + oscilloscope)
- The core glitch pattern exists in both versions.
- Old baseline: waveform instability is longer and recovery cycle is longer.
- Current checkpoint: instability cycle is shorter (roughly 2-4x faster), with crackle and phase-shift events appearing closer together.
- Reported short spikes/crackle:
  - spacing around ~5 ms
  - duration around ~400 us
  - spikes trend toward zero crossing but are not hard zero drops
- Reported phase-wave cycle around ~32 ms in recent tests.

## Conclusion
The "pre-i2c-dma was better" hypothesis is not supported.
The issue class appears unchanged across both versions; only temporal behavior changed.

Most likely contributing factors:
- USB clock drift control loop behavior (feedback servo dynamics)
- short-read / micro-gap behavior on USB input path
- core0 service jitter under mixed USB + UI + I2C load

## Why phase artifacts are visible on OLED but short crackle spikes are not
- OLED waveform display is heavily decimated for frame-rate display.
- Low-frequency phase drift remains visible.
- Sub-millisecond spikes are usually below display temporal resolution.

## Implemented direction in this checkpoint
- Bounded USB task pull budget (reduces core0 burst load)
- PI-style USB feedback control with bounded integrator
- Streaming state synchronization tightened with atomics
- core1 short-read concealment/blend to suppress click-like transients
- UI/OLED backpressure and async flush gating improvements
- MPR121 level read batched into one burst transfer

## Recommended next step
If the issue remains audible after this checkpoint, implement minimal elastic resampling (ASRC-lite) on USB input on core1 to fully decouple host USB clock and local I2S clock.
