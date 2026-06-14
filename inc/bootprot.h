#pragma once

// Shared BOOTPROT/fuse helpers used by the self-update tools (selfmain.c,
// selfmain_layoutv2.c). Must be included after uf2.h (for logval/resetIntoApp
// declarations and NVMCTRL types via sam.h).

#if defined(SAMD21)
#define NVM_FUSE_ADDR ((uint32_t *)NVMCTRL_AUX0_ADDRESS)
#elif defined(SAMD51)
#define NVM_FUSE_ADDR ((uint32_t *)NVMCTRL_FUSES_BOOTPROT_ADDR)
#endif

static inline void nvmctrl_wait_ready(void) {
#if defined(SAMD21)
    while (NVMCTRL->INTFLAG.bit.READY == 0) { }
#elif defined(SAMD51)
    while (NVMCTRL->STATUS.bit.READY == 0) { }
#endif
}

static inline void nvmctrl_set_addr(const uint32_t *addr) {
#if defined(SAMD21)
    NVMCTRL->ADDR.reg = (uint32_t)addr / 2;
#elif defined(SAMD51)
    NVMCTRL->ADDR.reg = (uint32_t)addr;
#endif
}

static inline void nvmctrl_exec_cmd(uint32_t cmd) {
#if defined(SAMD21)
    NVMCTRL->STATUS.reg |= NVMCTRL_STATUS_MASK;  // Clear error status bits.
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | cmd;
    nvmctrl_wait_ready();
#elif defined(SAMD51)
    NVMCTRL->CTRLB.reg = NVMCTRL_CTRLB_CMDEX_KEY | cmd;
#endif
    nvmctrl_wait_ready();
}

// Sets BOOTPROT to new_bootprot if it differs from the current value (or the
// fuse row looks erased). Writing the fuse row always ends in a hard reset
// via resetIntoApp() - code after a call that actually changes the fuses does
// not run; the caller is re-entered from scratch after reset.
static inline void set_fuses_and_bootprot(uint32_t new_bootprot) {
#if defined(SAMD21)
    uint32_t fuses[2];
#elif defined(SAMD51)
    uint32_t fuses[128];    // 512 bytes (whole user page)
#endif
    nvmctrl_wait_ready();

    memcpy(fuses, (uint32_t *)NVM_FUSE_ADDR, sizeof(fuses));

    // If it appears the fuses page was erased (all ones), replace fuses with reasonable values.

#if defined(SAMD21)
    bool repair_fuses = (fuses[0] == 0xffffffff ||
                         fuses[1] == 0xffffffff);
#elif defined(SAMD51)
    bool repair_fuses = (fuses[0] == 0xffffffff ||
                         fuses[1] == 0xffffffff ||
                         fuses[4] == 0xffffffff);
#endif

    if (repair_fuses) {
        // These canonical fuse values taken from working Adafruit boards.
        // BOOTPROT is set to nothing in these values.
#if defined(SAMD21)
        fuses[0] = 0xD8E0C7FF;
        fuses[1] = 0xFFFFFC5D;
#elif defined(SAMD51)
        fuses[0] = 0xFE9A9239;
        fuses[1] = 0xAEECFF80;
        fuses[2] = 0xFFFFFFFF;
        // fuses[3] is for user use, so we don't change it.
        fuses[4] = 0x00804010;
#endif
    }

    uint32_t current_bootprot = (fuses[0] & NVMCTRL_FUSES_BOOTPROT_Msk) >> NVMCTRL_FUSES_BOOTPROT_Pos;

    logval("repair_fuses", repair_fuses);
    logval("current_bootprot", current_bootprot);
    logval("new_bootprot", new_bootprot);

    // Don't write if nothing will be changed.
    if (current_bootprot == new_bootprot && !repair_fuses) {
        return;
    }

    // Update fuses BOOTPROT value with desired value.
    fuses[0] = (fuses[0] & ~NVMCTRL_FUSES_BOOTPROT_Msk) | (new_bootprot << NVMCTRL_FUSES_BOOTPROT_Pos);

    // Write the fuses.

#if defined(SAMD21)
    NVMCTRL->CTRLB.reg = NVMCTRL->CTRLB.reg | NVMCTRL_CTRLB_CACHEDIS | NVMCTRL_CTRLB_MANW;
    nvmctrl_set_addr(NVM_FUSE_ADDR);  // Set address to auxiliary row (fuses).
    nvmctrl_exec_cmd(NVMCTRL_CTRLA_CMD_EAR);  // Erase auxiliary row.
    nvmctrl_exec_cmd(NVMCTRL_CTRLA_CMD_PBC);  // Clear page buffer (64 bytes).
    // Writes must be 16 or 32 bits at a time.
    NVM_FUSE_ADDR[0] = fuses[0];
    NVM_FUSE_ADDR[1] = fuses[1];
    nvmctrl_exec_cmd(NVMCTRL_CTRLA_CMD_WAP);
#elif defined(SAMD51)
    NVMCTRL->CTRLA.bit.WMODE = NVMCTRL_CTRLA_WMODE_MAN;
    nvmctrl_set_addr(NVM_FUSE_ADDR);  // Set address to user page.
    nvmctrl_exec_cmd(NVMCTRL_CTRLB_CMD_EP);   // Erase user page.
    nvmctrl_exec_cmd(NVMCTRL_CTRLB_CMD_PBC);  // Clear page buffer.
    for (size_t i = 0; i < sizeof(fuses) / sizeof(uint32_t); i += 4) {
        // Copy a quadword, one 32-bit word at a time. Writes to page
        // buffer must be 16 or 32 bits at a time, so we use explicit
        // word writes
        NVM_FUSE_ADDR[i + 0] = fuses[i + 0];
        NVM_FUSE_ADDR[i + 1] = fuses[i + 1];
        NVM_FUSE_ADDR[i + 2] = fuses[i + 2];
        NVM_FUSE_ADDR[i + 3] = fuses[i + 3];
        nvmctrl_set_addr(&NVM_FUSE_ADDR[i]); // Set write address to the current quad word.
	nvmctrl_exec_cmd(NVMCTRL_CTRLB_CMD_WQW); // Write quad word.
    }
#endif

    resetIntoApp();
}
