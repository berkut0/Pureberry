# USB Audio + OLED Input Glitch Refactor Plan (v2)

Last updated: 2026-02-16

## 1) Problem Statement

Observed on hardware:
- USB audio input path is relatively stable with `ENABLE_OLED=OFF`.
- With `ENABLE_OLED=ON`, audible input artifacts appear: phase slips, short stalls, and spikes.
- Overclocking improves behavior but appears to mask timing pressure instead of removing root cause.

Current architecture facts in this repo:
- core1 runs Heavy + I2S (`core/src/audio_runtime.c`).
- core0 services USB + I2C + UI (`core/src/main.c`).
- USB host -> device audio uses core0 drain to ring and core1 pop from ring (`core/src/usb/usb_audio.c`).
- OLED flush is async DMA-backed (`core/src/dev/oled.c`), but UI draw and core0 scheduling still consume service budget.
- `multicore_display_capture_interleaved()` runs in core1 audio loop when `ENABLE_OLED` is enabled.

## 2) Ranked Hypotheses

H1 (highest probability):
- Core0 USB service cadence is insufficient under OLED/UI load.
- Result: ring fill dips, core1 short-reads, audible discontinuities.

Evidence:
- TinyUSB config already documents queue pressure with USB audio + OLED (`core/src/tusb_config.h`).
- `usb_audio_task()` has bounded per-call draining (`USB_AUDIO_MAX_PULL_CHUNKS_PER_TASK`).
- Artifacts improve with OC, consistent with service-budget shortage.

H2:
- Configuration coupling changes USB behavior when OLED is enabled.

Evidence:
- `USB_AUDIO_TARGET_FILL_FRAMES` defaults change under `ENABLE_OLED` in `core/src/config.h`.
- A/B comparisons may currently compare different transport operating points.

H3:
- `multicore_display_capture_interleaved()` adds enough core1 overhead to push runtime over edge in borderline cases.

Evidence:
- Function executes in core1 loop.
- Expected cost is moderate, but still part of hot path and should be measured, not assumed.

H4:
- Optional I2C peers (for example MPR121) increase contention and worsen timing in OLED builds.

## 3) Locked Constraints

1. Multicore contract remains strict:
- Heavy API calls only on core1 after audio start.
- I/O remains on core0.

2. RT safety:
- No blocking I/O/logging/malloc in core1 audio loop.

3. Transport semantics must not change silently:
- `ctrl_queue` overflow policy stays "drop newest".
- OLED and UI work stays outside core1 hardware I/O.

4. Minimalism:
- Prefer minimal diffs and avoid unnecessary new modules/files.

## 4) Refactor Goals

1. Remove audible USB input glitches with `ENABLE_OLED=ON` without relying on overclocking.
2. Make root-cause validation measurable (not ear-based only).
3. Keep code deterministic and easier to reason about.
4. Preserve existing feature set and project invariants.

## 5) Non-Goals

1. No new USB class features or descriptor redesign.
2. No architectural migration to RTOS/tasks.
3. No broad UI redesign outside what is needed for audio stability.

## 6) Execution Plan

### Phase A: Observability First (No Behavior Change)

Actions:
1. Add explicit runtime diagnostics for USB input stability:
- ring fill snapshot/min/max window,
- underrun delta per second,
- overrun delta per second,
- last RX bytes / available bytes trends.
2. Expose diagnostics in existing System/Advanced UI stats path.
3. Add a low-cost core1 counter for "short-read blocks" in USB input path.

Target files:
- `core/src/usb/usb_audio.c`
- `core/src/usb/usb_audio.h`
- `core/src/audio_runtime.c`
- `core/src/ui/system/ui_system_stats.c`
- `core/src/ui/system/ui_system_stats.h`
- `core/src/ui/mui/ui_mui_forms.c`

Exit criteria:
- We can correlate audible events with underrun/short-read telemetry.

### Phase B: Remove Confounding A/B Variables

Actions:
1. Decouple default `USB_AUDIO_TARGET_FILL_FRAMES` from `ENABLE_OLED`.
2. Keep OLED-specific tuning as explicit override in `config_local.h`, not hidden in feature toggle.
3. Add comments documenting why this decoupling is needed for valid diagnostics.

Target files:
- `core/src/config.h`
- `core/src/config_local.h.example` (doc comment only)

Exit criteria:
- OLED ON/OFF comparison does not silently change USB ring target unless explicitly configured.

### Phase C: Core0 Service Cadence Refactor (Primary)

Actions:
1. Rework `service_peripherals()` ordering to prioritize USB audio drain cadence during streaming.
2. Ensure no long section executes without re-entering `service_usb()`.
3. Add a "UI backpressure by audio state" guard:
- if USB audio is streaming and ring fill is below low watermark, skip one UI render cycle.
4. Keep behavior deterministic and branch-light.

Target files:
- `core/src/main.c`
- `core/src/ui/ui_manager.c` (if cadence gate is placed there)
- `core/src/usb/usb_audio.c` (only if helper accessors are needed)

Exit criteria:
- Underrun counter remains flat in steady-state playback with OLED active.

### Phase D: Core1 Hot-Path Isolation (Secondary)

Actions:
1. Introduce a dedicated compile-time switch for waveform capture path used by OLED UI feedback.
2. Keep default behavior unchanged, but enable clean A/B without touching unrelated code.
3. Measure impact of disabling capture under identical USB settings.

Target files:
- `core/src/audio_runtime.c`
- `core/src/multicore_display.c`
- `core/src/config.h` (switch definition)

Exit criteria:
- Quantified answer whether capture is causal, contributory, or negligible.

### Phase E: OLED Workload Shaping (Only If Needed)

Actions:
1. If Phases B-C do not fully stabilize audio, reduce waveform rendering cost under streaming mode:
- lower streaming FPS, and/or
- reduce per-frame draw complexity.
2. Keep UI responsive and readable.

Target files:
- `core/src/ui/screens/ui_screen_waveform.c`
- `core/src/config.h`
- `core/src/config_local.h.example`

Exit criteria:
- No audible discontinuities under target workload without overclock requirement.

## 7) Verification Matrix

Build entrypoint (mandatory):
1. `python scripts/build_firmware.py .\pd-patches\adc_to_dac_knob1.pd --clean -D ENABLE_USB_AUDIO=ON -D ENABLE_OLED=OFF`
2. `python scripts/build_firmware.py .\pd-patches\adc_to_dac_knob1.pd --clean -D ENABLE_USB_AUDIO=ON -D ENABLE_OLED=ON`
3. `python scripts/build_firmware.py .\pd-patches\adc_to_dac_knob1.pd --clean -D ENABLE_USB_AUDIO=ON -D ENABLE_OLED=ON -D ENABLE_MPR121=ON` (if used in real hardware setup)

Runtime tests:
1. 20-30 min host playback at 48 kHz stereo with OLED waveform screen active.
2. Repeat with menu screens and normal UI actions.
3. Record telemetry snapshots every 1 s:
- underrun count delta,
- ring fill trend,
- short-read blocks,
- core1 DSP average load.

Acceptance criteria:
1. No sustained underrun growth in steady-state with OLED enabled.
2. No audible phase slips/spikes during long-run test.
3. Behavior remains stable at baseline clock profile (without mandatory OC).

## 8) Risk Register

1. Risk:
- Over-prioritizing USB service can starve other core0 tasks.

Mitigation:
- Use bounded gating and verify UI/input/peripheral responsiveness.

2. Risk:
- Added diagnostics increase overhead and influence timing.

Mitigation:
- Keep counters simple and constant-time; avoid heavy logging in hot paths.

3. Risk:
- Hidden board-specific factors (USB host stack, cable quality, power noise) can mimic firmware timing faults.

Mitigation:
- Repeat tests across at least two host ports/cables and one alternate board power path.

## 9) Rollback and Decision Gates

1. After Phase A:
- If underruns do not correlate with artifacts, pause and reassess root cause before Phase C.

2. After Phase C:
- If artifacts are materially reduced, keep cadence refactor and avoid deeper changes.

3. After Phase D:
- If capture impact is negligible, keep capture enabled and avoid unnecessary complexity.

4. If any phase increases complexity without measurable benefit:
- rollback that phase and keep the simpler variant.
