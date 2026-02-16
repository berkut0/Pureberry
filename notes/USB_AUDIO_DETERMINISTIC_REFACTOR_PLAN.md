# USB Audio Deterministic Path Refactor Plan (v1)

Last updated: 2026-02-16

## 1) Context

Current status:
- catastrophic "runaway/scream" events were mitigated by output safety guards,
- rare short stutters still happen (roughly once in 15-60 seconds, sometimes rarer),
- I2C/MPR121 is no longer the primary suspect for these residual stutters.

Observed architecture facts in this repo:
- USB audio RX is handled on core0 (`core/src/usb/usb_audio.c`),
- Heavy + I2S rendering runs on core1 (`core/src/audio_runtime.c`),
- core1 currently includes a runtime USB-rate branch with resampling logic and FIFO shifting.

Working hypothesis:
- residual stutter is caused by variable execution cost and timing jitter in the core1 USB input path, not by missing "extra DMA for USB".

## 2) Locked Constraints

1. Priorities:
- determinism in audio path,
- maintainability and readability,
- minimalism (no unnecessary entities/files/APIs).

2. No backward-compatibility burden for old behavior:
- we can simplify aggressively if resulting architecture is cleaner and safer.

3. Layering:
- USB transport details stay in `usb_audio.*`,
- Heavy/I2S runtime stays in `audio_runtime.*`,
- no DMA/I2C leakage into USB audio architecture.

4. Scope discipline:
- do not redesign unrelated subsystems in this iteration.

## 3) Goals

1. Make USB input path to Heavy deterministic per audio block.
2. Remove runtime complexity that causes rare timing spikes.
3. Keep public usage simple for patch/device code.
4. Preserve stable behavior with `--usb-audio --oled --mpr121`.

## 4) Non-Goals

1. No new USB audio features (mic path, multirate expansion, new descriptors).
2. No extra observability framework in this sprint.
3. No generic RTOS/task-system redesign.

## 5) Architecture Decision

Decision for this refactor:
- lock USB audio stream to 48 kHz in this firmware build,
- remove runtime sample-rate adaptation/resampler path from core1,
- keep one deterministic "pop + convert + process" flow per block.

Rationale:
- Heavy/I2S runtime is fixed at 48 kHz already,
- a fixed-rate path reduces branchy logic, memmove activity, and jitter risk,
- this is the smallest change with the best probability of removing rare stutter.

## 6) Execution Plan

### Phase A: Freeze Runtime Contract to 48 kHz

Actions:
1. Ensure accepted USB sample rate in control handlers is 48 kHz only.
2. Keep descriptors/config aligned to 48 kHz-only operation for this phase.
3. Keep behavior explicit in comments/docs.

Files:
- `core/src/usb/usb_audio.c`
- `core/src/tusb_config.h` (only if needed for consistency wording/macros)

Exit:
- no runtime "sample-rate switch" path remains active.

### Phase B: Remove Core1 Runtime Resampler Branch

Actions:
1. Delete/disable resampler FIFO state from `audio_core1_main`.
2. Keep single fixed path:
- pop interleaved int16 frames from USB ring,
- zero-fill on short read,
- convert to float,
- feed Heavy input buffer.
3. Keep existing priming logic only if it is still necessary and simple.

Files:
- `core/src/audio_runtime.c`

Exit:
- core1 USB input handling is branch-minimal and deterministic.

### Phase C: Simplify USB RX Pull Loop (Core0)

Actions:
1. Keep one straightforward FIFO-drain policy in `usb_audio_task`.
2. Avoid unnecessary per-iteration complexity while preserving bounds checks.
3. Keep feedback update behavior stable after simplification.

Files:
- `core/src/usb/usb_audio.c`

Exit:
- core0 RX path remains robust but easier to reason about.

### Phase D: Docs Sync

Actions:
1. Add a short section that this firmware revision is 48 kHz fixed for USB input path determinism.
2. Document why this is intentional and what would be needed to reintroduce multirate later.

Files:
- `notes/USB_AUDIO_PC_TO_RP2350_IMPLEMENTATION.md`
- optionally `notes/I2C_DEVELOPER_GUIDE.md` (only if cross-reference is needed)

Exit:
- architecture and constraints are explicit for future contributors.

## 7) Verification Protocol

Build entrypoint (mandatory):
1. `python scripts/build_firmware.py .\pd-patches\adc_to_dac.pd --clean --usb-audio --mpr121 --oled`
2. `python scripts/build_firmware.py .\pd-patches\adc_to_dac.pd --clean --usb-audio`

Hardware checks:
1. 20-30 min continuous run with host audio input.
2. Verify no severe runaway events and reduced/absent rare stutter.
3. Repeat with OLED enabled and with periodic MPR121 touches.
4. Confirm no regressions in normal audio-through behavior (`adc~ -> dac~`).

Acceptance criteria:
1. deterministic USB input path in code (no runtime resampler branch),
2. stable audible behavior in long-run test,
3. code complexity reduced versus pre-refactor version.

## 8) Risks and Fallback

Risk:
- some hosts may request unsupported sample-rate controls more aggressively.

Handling:
1. respond predictably (reject unsupported rates, remain at 48 kHz),
2. keep stream state coherent on host alt-setting changes,
3. if needed, tighten host-control handling without reintroducing runtime resampling.

## 9) Deferred Work (Next Sprint Candidate)

1. Reintroduce multirate support only if required by real use-case evidence.
2. If reintroduced, keep it in transport-level rate adaptation, not in core1 hot path.
3. Consider a stricter execution budget review for core0 main loop scheduling under OLED load.
