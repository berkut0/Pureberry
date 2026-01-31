/*
 * Crash reporting and reboot hooks.
 *
 * - Captures HardFault details (PC/LR + SCB fault status) and reboots via watchdog.
 * - Overrides pico-sdk panic() via PICO_PANIC_FUNCTION (see pico_config.h) to reboot instead of hanging.
 *
 * NOTE: Handlers must be safe in fault context:
 * - No printf, malloc, or peripheral I/O.
 * - Only write watchdog scratch registers and trigger a reset.
 */

#include "crash.h"

#include "hardware/regs/psm.h"
#include "hardware/regs/watchdog.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/watchdog.h"

#define CRASH_MAGIC 0x43525348u // 'CRSH'

// Scratch register layout (keep scratch[4] free for pico-sdk watchdog_enable magic).
enum {
    CRASH_SCRATCH_MAGIC = 0,
    CRASH_SCRATCH_REASON = 1,
    CRASH_SCRATCH_PC = 2,
    CRASH_SCRATCH_LR = 3,
    // scratch[4] is reserved by pico-sdk watchdog_enable/watchdog_reboot
    // scratch[5..7] are "extra" fields that are interpreted depending on crash reason:
    // - HardFault: CFSR/HFSR/BFAR
    // - Panic: arg0/0/0 (arg0 = first variadic argument to panic(fmt, ...))
    CRASH_SCRATCH_EXTRA0 = 5,
    CRASH_SCRATCH_EXTRA1 = 6,
    CRASH_SCRATCH_EXTRA2 = 7,
};

static inline uint8_t crash_core_id(void) {
    // On RP2350, SIO->CPUID bit0 indicates core (0/1).
    return (uint8_t)(sio_hw->cpuid & 1u);
}

static inline void crash_store(crash_reason_t reason, uint32_t pc, uint32_t lr, uint32_t extra0, uint32_t extra1, uint32_t extra2) {
    watchdog_hw->scratch[CRASH_SCRATCH_MAGIC] = CRASH_MAGIC;
    watchdog_hw->scratch[CRASH_SCRATCH_REASON] = ((uint32_t)reason & 0xFFu) | (((uint32_t)crash_core_id() & 0xFFu) << 8);
    watchdog_hw->scratch[CRASH_SCRATCH_PC] = pc;
    watchdog_hw->scratch[CRASH_SCRATCH_LR] = lr;
    watchdog_hw->scratch[CRASH_SCRATCH_EXTRA0] = extra0;
    watchdog_hw->scratch[CRASH_SCRATCH_EXTRA1] = extra1;
    watchdog_hw->scratch[CRASH_SCRATCH_EXTRA2] = extra2;
}

static inline void crash_trigger_reboot(void) {
    // Configure watchdog reset selection similar to pico-sdk watchdog_enable():
    // reset everything except ROSC/XOSC, then trigger immediately.
    watchdog_hw->ctrl &= ~WATCHDOG_CTRL_ENABLE_BITS;
    psm_hw->wdsel |= (PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));
    watchdog_hw->ctrl &= ~(WATCHDOG_CTRL_PAUSE_DBG0_BITS | WATCHDOG_CTRL_PAUSE_DBG1_BITS | WATCHDOG_CTRL_PAUSE_JTAG_BITS);
    watchdog_hw->ctrl |= WATCHDOG_CTRL_TRIGGER_BITS;

    // Wait for reset.
    while (true) {
        __asm volatile("wfe");
    }
}

bool crash_read(crash_info_t *out) {
    if (!out) return false;
    if (watchdog_hw->scratch[CRASH_SCRATCH_MAGIC] != CRASH_MAGIC) return false;

    uint32_t reason_word = watchdog_hw->scratch[CRASH_SCRATCH_REASON];
    out->reason = (crash_reason_t)(reason_word & 0xFFu);
    out->core_id = (uint8_t)((reason_word >> 8) & 0xFFu);
    out->pc = watchdog_hw->scratch[CRASH_SCRATCH_PC];
    out->lr = watchdog_hw->scratch[CRASH_SCRATCH_LR];

    uint32_t const extra0 = watchdog_hw->scratch[CRASH_SCRATCH_EXTRA0];
    uint32_t const extra1 = watchdog_hw->scratch[CRASH_SCRATCH_EXTRA1];
    uint32_t const extra2 = watchdog_hw->scratch[CRASH_SCRATCH_EXTRA2];

    if (out->reason == CRASH_REASON_PANIC) {
        out->arg0 = extra0;
        out->cfsr = 0u;
        out->hfsr = 0u;
        out->bfar = 0u;
    } else {
        out->arg0 = 0u;
        out->cfsr = extra0;
        out->hfsr = extra1;
        out->bfar = extra2;
    }
    return true;
}

void crash_clear(void) {
    watchdog_hw->scratch[CRASH_SCRATCH_MAGIC] = 0u;
    watchdog_hw->scratch[CRASH_SCRATCH_REASON] = 0u;
    watchdog_hw->scratch[CRASH_SCRATCH_PC] = 0u;
    watchdog_hw->scratch[CRASH_SCRATCH_LR] = 0u;
    watchdog_hw->scratch[CRASH_SCRATCH_EXTRA0] = 0u;
    watchdog_hw->scratch[CRASH_SCRATCH_EXTRA1] = 0u;
    watchdog_hw->scratch[CRASH_SCRATCH_EXTRA2] = 0u;
}

//--------------------------------------------------------------------+
// Panic hook (PICO_PANIC_FUNCTION)
//--------------------------------------------------------------------+

static void __attribute__((noreturn, used)) crash_panic_c(uint32_t caller_lr, uint32_t fmt_ptr, uint32_t arg0) {
    // panic() (when PICO_PANIC_FUNCTION is set) pushes the original caller LR on the stack
    // and then branches to this handler. We store that return address as "pc".
    uint32_t pc = caller_lr & ~1u; // clear Thumb bit for offline addr2line
    crash_store(CRASH_REASON_PANIC, pc, fmt_ptr, arg0, 0u, 0u);
    crash_trigger_reboot();
}

void __attribute__((naked, noreturn)) project_panic_handler(const char *fmt, ...) {
    (void)fmt;
    __asm volatile(
        // On entry:
        // - r0 = fmt pointer (first arg to panic())
        // - r1 = first variadic argument (if any)
        // - [sp] = LR of the caller of panic() (pushed by panic()'s wrapper)
        "ldr r2, [sp, #0]\n" // r2 = caller LR
        "mov r3, r0\n"       // r3 = fmt pointer
        "mov r0, r2\n"       // arg0 = caller LR
        "mov r2, r1\n"       // arg2 = vararg0
        "mov r1, r3\n"       // arg1 = fmt pointer
        "b crash_panic_c\n"
    );
}

//--------------------------------------------------------------------+
// HardFault handler
//--------------------------------------------------------------------+

static void __attribute__((noreturn)) crash_hardfault_c(uint32_t *sp) {
    // Exception stack frame: r0,r1,r2,r3,r12,lr,pc,xpsr
    uint32_t lr = sp ? sp[5] : 0u;
    uint32_t pc = sp ? sp[6] : 0u;
    lr &= ~1u;
    pc &= ~1u;

    crash_store(CRASH_REASON_HARDFAULT, pc, lr, scb_hw->cfsr, scb_hw->hfsr, scb_hw->bfar);
    crash_trigger_reboot();
}

void __attribute__((naked)) HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b crash_hardfault_c\n"
    );
}
