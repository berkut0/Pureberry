# IO Bus Refactor Notes (I2C now, SPI later)
Last updated: 2026-02-06

This note captures the **bus-layer** refactor discussion and the audit findings around I2C usage, scalability to multiple peripherals, and documentation drift.

Status (2026-02-06):
- Phase 1 implemented: `i2c_bus` layer added, OLED/MPR121 decoupled, bus config macros expanded, DMA arbitration centralized, core0 polls bus.

The goal is to document:
- What the codebase does today (and where the hidden coupling is).
- Why this becomes a maintenance problem as more peripherals appear.
- A **best-practice bus-layer architecture** that scales to:
  - multiple I2C devices on one bus,
  - both I2C controllers (i2c0 + i2c1),
  - future SPI peripherals,
  - optional DMA optimizations without cross-module coupling.
- A staged migration plan and a documentation consistency checklist.

All examples below are written for RP2040/RP2350 + Pico SDK, but the principles are generic embedded best practice.

---

## 1) Context and architectural constraints

### Multicore contract (important for any bus work)
The firmware has a strict split:
- **core1**: audio DSP (Heavy), RT context; no blocking I/O and no `printf`.
- **core0**: initialization and all I/O (USB, WS2812, OLED, MPR121, future sensors).

Implication for buses:
- **All I2C/SPI activity must remain on core0**.
- If future features want to trigger bus work from audio/sendHook, they must enqueue work for core0 (same pattern as LED queue).

---

## 2) What exists today (observations from code)

### 2.1 I2C configuration macros
`core/src/config.h` introduced neutral I2C macros:
- `I2C_BUS_INSTANCE`, `I2C_BUS_SDA_PIN`, `I2C_BUS_SCL_PIN`, `I2C_BUS_BAUD`, `I2C_BUS_TIMEOUT_US`
- `I2C_GET_INSTANCE(instance_num)` helper macro

Backwards compatibility is preserved by mapping `OLED_I2C_*` to `I2C_BUS_*`.

This is a good direction, but it is still a **single-bus** model.

### 2.2 Where I2C is initialized
Currently, I2C init (pin mux + pullups + `i2c_init`) happens inside the OLED driver (`core/src/dev/oled.c`).

This means:
- If OLED is disabled, the I2C bus is not initialized anywhere.
- Any I2C device that expects the bus to exist has a hidden dependency on OLED (or on “someone else” having done bus init).

### 2.3 MPR121 driver still depends on “external” bus init
`core/src/dev/mpr121_touch.c` probes and talks to the device, but does not configure:
- SDA/SCL GPIO functions,
- I2C baud rate,
- bus recovery.

The header even documents the dependency:

> “Call after I2C bus is inited (e.g. after oled_init()).”

This is a real functional risk: `--mpr121` without `--oled` may build but not work on hardware.

### 2.4 u8g2 backend uses timeouts (good)
`core/src/dev/u8g2_pico.c` uses `i2c_write_timeout_us(...)` with `I2C_BUS_TIMEOUT_US`, which is good: it avoids hard hangs on stuck buses.

### 2.5 DMA helper exists but is not a bus arbiter
`core/src/drv/i2c_dma.[ch]` implements non-blocking TX via DMA and already has good properties:
- per-`i2c_inst_t` intent (“serialized per instance”) and timeout-protected completion,
- explicit STOP/RESTART control via IC_DATA_CMD word streams,
- completion callbacks invoked from poll (not from IRQ).

However, **it is not a complete bus arbitration layer**:
- It only serializes its own queued DMA TX transactions.
- Any “normal” `i2c_*_blocking` read/write in another driver can still collide with DMA activity.
- It can also reset the controller on abort (toggling `hw->enable`), which affects all devices on that controller.

---

## 3) Why this becomes a problem with more peripherals

As the project grows, likely changes include:
- more I2C peripherals (encoders, IMUs, expanders, ADCs, etc.),
- using **both** I2C controllers (i2c0 + i2c1) for:
  - different speed requirements (1 MHz vs 400 kHz),
  - physical wiring constraints (pin mux),
  - isolation of high-traffic devices (OLED refresh) from sensor reads,
- future SPI peripherals (displays, flash, DACs, ADCs, radios).

If bus init and bus policy live inside leaf drivers (OLED/MPR121), you get:
- hidden init order requirements,
- duplicated pin mux and recovery code,
- “last one wins” configuration (baud rate and controller settings),
- non-deterministic bugs when DMA and blocking I/O overlap,
- exploding support/debug burden as combinations grow.

**Conclusion:** bus init and bus policy should be owned by a dedicated, neutral bus layer.

---

## 4) Design principles for a scalable bus layer

### 4.1 Bus init must not live in leaf drivers
Leaf drivers should not:
- decide pin mux for the controller,
- call `i2c_init` / configure `hw->tar` / reset the controller,
- contain bus recovery logic (SCL toggling etc.).

Leaf drivers should:
- declare which bus they use,
- set only device-specific configuration (address, IRQ/CS pin, protocol details),
- perform transfers through a bus API.

### 4.2 Avoid a user-facing `ENABLE_I2C` flag
An “I2C enabled” feature flag tends to be a foot-gun:
- “I enabled MPR121 but forgot ENABLE_I2C” type failures.

Preferred approach:
- bus infrastructure is always compiled (small and stable),
- bus initialization happens when an enabled device needs it.

### 4.3 Prefer timeouts everywhere
Blocking I2C transactions without timeouts can hard-hang core0 if the bus is stuck.

The bus layer should offer:
- `write_timeout`, `read_timeout`, `write_read_timeout`, `probe_timeout`,
and leaf drivers should use them by default.

### 4.4 Configuration must scale to 2 controllers
A single `I2C_BUS_*` is fine for today, but it does not represent “two buses”.

The config should evolve toward **two independent bus configs**, e.g.:
- `I2C_BUS0_*` and `I2C_BUS1_*` (or `I2C_BUS_A/B_*`),
and device mapping:
- `OLED_I2C_BUS_ID`, `MPR121_I2C_BUS_ID`, etc.

### 4.5 Arbitration must be explicit once multiple devices share a bus
When there are multiple devices on one controller:
- DMA TX + blocking reads/writes must not overlap.
- Even two “blocking” drivers can conflict if they reconfigure the controller differently.

The simplest best practice: **single owner of the bus** with explicit serialization.

---

## 5) Proposed target architecture

### 5.1 Files/modules
Introduce a new neutral module (names are suggestions):
- `core/src/drv/i2c_bus.h`
- `core/src/drv/i2c_bus.c`

Later, mirror for SPI:
- `core/src/drv/spi_bus.h`
- `core/src/drv/spi_bus.c`

### 5.2 Responsibilities of `i2c_bus`
`i2c_bus` owns:
- bus configuration (instance/pins/baud/timeout policy),
- `gpio_set_function(..., GPIO_FUNC_I2C)` and pullups,
- bus recovery (SCL toggling, STOP generation),
- `i2c_init()` and any controller resets,
- (optional) integration with `i2c_dma` (TX) with safe arbitration.

Leaf drivers call:
- `i2c_bus_init_once(bus_id)`
- `i2c_bus_get_inst(bus_id)` (if absolutely needed for third-party APIs),
or better:
- `i2c_bus_write/read/write_read` wrappers with timeouts.

### 5.3 Suggested public API (minimal)
Example API surface (final naming is up to the repo):

```c
typedef enum {
    I2C_BUS_ID_0 = 0,
    I2C_BUS_ID_1 = 1,
} i2c_bus_id_t;

typedef struct {
    i2c_inst_t *inst;        // i2c0 or i2c1
    uint sda_pin;
    uint scl_pin;
    uint32_t baud_hz;
    uint32_t timeout_us;     // per-transaction timeout
} i2c_bus_config_t;

bool i2c_bus_init_once(i2c_bus_id_t id);
const i2c_bus_config_t *i2c_bus_get_config(i2c_bus_id_t id);

int i2c_bus_write_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop);
int i2c_bus_read_timeout(i2c_bus_id_t id, uint8_t addr7, uint8_t *buf, size_t len);
bool i2c_bus_probe(i2c_bus_id_t id, uint8_t addr7);
void i2c_bus_recover(i2c_bus_id_t id);
```

Notes:
- Return types should match Pico SDK patterns where possible.
- Provide a consistent place for timeouts and error translation.

### 5.4 DMA integration options (trade-offs)

#### Option A (staged, simplest first): bus init only
Phase 1 can implement only:
- config + init_once + recover + timeouts
and leave DMA arbitration as-is.

Pros: minimal change, immediate removal of hidden init order.
Cons: still risk of collisions if multiple I2C users coexist (OLED DMA + other reads).

#### Option B (recommended once multiple I2C devices are active): bus-level serialization
Make `i2c_bus` the only place that can start DMA transactions and perform blocking reads/writes.

Approach:
- represent all I2C work as “transactions” (write/read/write_read),
- allow async DMA TX for large writes (OLED framebuffer),
- ensure **no blocking transfer happens while DMA is active**, and vice versa,
- drive completion from core0 main loop (poll), similar to how `i2c_dma_poll()` works today.

Why this is important:
- `i2c_dma_abort_active()` resets controller state; a leaf driver cannot be allowed to do that behind another driver’s back.

#### Option C: avoid mixing DMA and reads on the same bus
If the board wiring permits, dedicate:
- one controller to OLED refresh (DMA heavy),
- another controller to sensor reads.

This is the cleanest in runtime behavior, but you cannot rely on it for all users/boards.

---

## 6) Configuration model for 2 I2C controllers

### Recommended evolution path (keep today working)
**Stage 0 (today):** one shared config `I2C_BUS_*` and devices implicitly share it.

**Stage 1:** introduce explicit bus IDs:
- `I2C_BUS0_*` and `I2C_BUS1_*` in `config.h`
- `OLED_I2C_BUS_ID`, `MPR121_I2C_BUS_ID` (defaults to 0)

Then, transitional compatibility:
- map legacy `OLED_I2C_*` to the bus config referenced by `OLED_I2C_BUS_ID`
- keep old names for one release to avoid breaking local configs.

Rationale:
- Two controllers often need different baud rates and different pins.
- Pins are not arbitrary: the I2C function mux is flexible, but still constrained by the chip’s pin mux table; document that configs must be validated against the datasheet / Pico SDK mapping.

---

## 7) Migration plan (phased and safe)

### Phase 1: remove hidden coupling (functional correctness)
1) Create `drv/i2c_bus.[ch]` with:
   - init_once (pin mux + pullups + i2c_init)
   - recover() (move logic currently in `oled_i2c_bus_recover`)
   - timeout-based helpers
2) Update OLED:
   - replace direct pin mux + i2c_init with `i2c_bus_init_once(OLED_I2C_BUS_ID)`
3) Update MPR121:
   - call `i2c_bus_init_once(MPR121_I2C_BUS_ID)` before probing/initializing
   - replace blocking probe with timeout-based probe via bus API
4) Update header comments:
   - remove “call after oled_init()” dependency from `mpr121_touch.h`

Success criterion:
- `--mpr121` works on hardware with OLED disabled.

### Phase 2: arbitration + DMA safety (when multiple I2C devices share a bus)
1) Decide the policy:
   - allow mixing DMA writes + blocking reads only through bus-layer serialization, or
   - mandate “no DMA on shared bus unless all clients use bus transactions”.
2) Move ownership of `i2c_dma_t` context to bus-layer (per bus).
3) Provide a bus transaction queue:
   - OLED submits “write frame” TX transactions,
   - MPR121 submits read/write_read transactions,
   - bus layer schedules them one at a time.

Success criteria:
- no I2C collisions under stress (OLED refresh + frequent MPR121 reads),
- no hangs on stuck bus (timeouts + recover).

### Phase 3: expand config to 2 buses
1) Introduce `I2C_BUS0_*` + `I2C_BUS1_*` and device mapping macros.
2) Update `config_local.h.example` to document the new model.
3) Keep backward compatibility for legacy `OLED_I2C_*` for a transition window.

### Phase 4: mirror the pattern for SPI (no implementation yet, but lock the style)
Define a consistent “bus-layer pattern” for SPI:
- config macros per bus,
- init_once,
- device drivers configure only CS and device protocol,
- optional DMA later, owned by bus-layer.

---

## 8) Testing and validation checklist (bus-focused)

### Build matrix
- base default (no flags)
- `--oled`
- `--mpr121`
- `--oled --mpr121`
- (if relevant) `--i2c-dma` + combos above

### Hardware validation
1) `--mpr121` without OLED:
   - verify bus init is done and touch works
2) OLED-only:
   - verify display init and refresh is stable
3) OLED + MPR121:
   - stress: max OLED refresh + rapid touch events
   - look for missed touches, stuck bus, watchdog resets, or stalls
4) Inject bus fault:
   - pull SDA low briefly / disconnect device to confirm timeouts + recovery behavior

### Instrumentation (recommended)
Add bus-layer counters:
- init count (should be 1),
- recover count,
- timeout count,
- abort count (DMA abort source if used),
- queue depth high-water marks.

---
## 9) Quick reference (current CLI flags)

Single-argument feature flags:
- `--no-ws2812` (WS2812 is ON by default)
- `--oled`
- `--mpr121`
- `--usb-midi`
- `--i2c-dma`

Defaults (when a flag is omitted) come from CMake options.

---

## 10) Summary

### The core insight
I2C (and later SPI) should be treated as **shared infrastructure**, not as an implementation detail inside leaf drivers.

### Immediate risk to fix
MPR121 is not self-contained: it still depends on OLED (or some other module) initializing the I2C bus.

### The scalable solution
Add a neutral `i2c_bus` layer that owns:
- bus init + recovery,
- timeouts,
- controller resets,
- DMA integration/arbitration (when needed),
and make all I2C drivers go through it.

