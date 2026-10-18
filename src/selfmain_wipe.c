#include "uf2.h"

#include "sam.h"
#include "bootprot.h"

// Full-flash "wiper" update tool (bootloader v4.2.0+). Reinstalls the 16KB
// bootloader exactly like selfmain.c, and then erases the *complete* rest of
// the flash back to the erased state (0xFF): application (OTA_SLOT0), OTA
// staging bank (OTA_SLOT1), OTA_SETTINGS, OTA_VERSIONS and the OTA A/B flag
// row. The device ends up in the same state as a factory-fresh board that was
// programmed over SWD: a valid bootloader and nothing else.
//
// Use this on devices that are suspected to carry stale or corrupt content
// somewhere in flash (e.g. a SETTINGS.TXT that cannot be rewritten). Note that
// this destroys the device's configuration - firmware and settings.txt have to
// be written again afterwards.
//
// Not built by default; see "make wipe" / WIPER=1 in the Makefile. The
// resulting file is named update-WIPE-ALL-FLASH-*.uf2 so it cannot be confused
// with the regular bootloader updater.
//
// How this works: erasing the application area also erases this program, which
// runs from APP_START_ADDRESS. The erase loop - and everything that has to
// happen after it - therefore lives in wipe_flash_and_reset(), which is linked
// into the .ramfunc section and copied to RAM by the startup code before main()
// runs. That function must not call anything that lives in flash and must not
// read from the flash array.

#if !defined(SAMD21)
#error "The full-flash wiper is only implemented for SAMD21"
#endif

#define BOOTLOADER_K 16

extern const uint8_t bootloader[];
extern const uint16_t bootloader_crcs[];

uint8_t bootloader_page_buf[FLASH_ROW_SIZE];

// Runs from RAM. Erases [start_addr, FLASH_SIZE), restores BOOTPROT and resets
// into the freshly written bootloader. Never returns.
//
// Everything here is written out inline on purpose: after the first erased row
// there is no valid code left in the application flash area, so a call to a
// flash-resident helper (or a literal pool placed in flash) would hard fault.
__attribute__((section(".ramfunc"), noinline)) static void
wipe_flash_and_reset(uint32_t start_addr) {
    // Keep the NVM cache out of the way and use manual page writes (as the
    // fuse write below expects).
    NVMCTRL->CTRLB.reg |= NVMCTRL_CTRLB_CACHEDIS | NVMCTRL_CTRLB_MANW;

    LED_ERR_ON();

    // Unlock all 16 NVM regions. A device that ended up with a region lock bit
    // set silently refuses every erase/write in that region - which looks
    // exactly like "SETTINGS.TXT cannot be written, on any host OS". UR only
    // changes the volatile LOCK register; the fuse write further down makes it
    // permanent.
    for (uint32_t region = 0; region < 16u; ++region) {
        while (NVMCTRL->INTFLAG.bit.READY == 0) {
        }
        NVMCTRL->STATUS.reg = NVMCTRL_STATUS_MASK;
        NVMCTRL->ADDR.reg = (region * (FLASH_SIZE / 16u)) >> 1;
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_UR;
        while (NVMCTRL->INTFLAG.bit.READY == 0) {
        }
    }

    for (uint32_t addr = start_addr; addr < FLASH_SIZE; addr += FLASH_ROW_SIZE) {
        while (NVMCTRL->INTFLAG.bit.READY == 0) {
        }
        NVMCTRL->STATUS.reg = NVMCTRL_STATUS_MASK;
        NVMCTRL->ADDR.reg = addr >> 1;
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
        while (NVMCTRL->INTFLAG.bit.READY == 0) {
        }
        LED_ERR_TGL();
    }

    // Re-enable BOOTPROT for the 16KB bootloader. This is the same sequence as
    // set_fuses_and_bootprot(1) in bootprot.h, open coded because that helper
    // uses memcpy() and resetIntoApp(), both of which are gone by now. The
    // fuse (AUX0) row is not part of the main flash array, so it survived the
    // erase loop above and can still be read.
    uint32_t fuse0 = NVM_FUSE_ADDR[0];
    uint32_t fuse1 = NVM_FUSE_ADDR[1];

    if (fuse0 == 0xFFFFFFFFu || fuse1 == 0xFFFFFFFFu) {
        // Same canonical values as bootprot.h uses to repair an erased row.
        fuse0 = 0xD8E0C7FFu;
        fuse1 = 0xFFFFFC5Du;
    }
    fuse0 = (fuse0 & ~NVMCTRL_FUSES_BOOTPROT_Msk) | (1u << NVMCTRL_FUSES_BOOTPROT_Pos);
    // All region lock bits to 1 (= unlocked), so the unlock above survives the
    // reset. BOOTPROT keeps the bootloader area protected either way.
    fuse1 |= NVMCTRL_FUSES_REGION_LOCKS_Msk;

    while (NVMCTRL->INTFLAG.bit.READY == 0) {
    }
    NVMCTRL->ADDR.reg = ((uint32_t)NVM_FUSE_ADDR) >> 1;

    // Erase auxiliary row.
    NVMCTRL->STATUS.reg |= NVMCTRL_STATUS_MASK;
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_EAR;
    while (NVMCTRL->INTFLAG.bit.READY == 0) {
    }

    // Clear page buffer.
    NVMCTRL->STATUS.reg |= NVMCTRL_STATUS_MASK;
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
    while (NVMCTRL->INTFLAG.bit.READY == 0) {
    }

    // Writes must be 16 or 32 bits at a time.
    NVM_FUSE_ADDR[0] = fuse0;
    NVM_FUSE_ADDR[1] = fuse1;

    // Write auxiliary page.
    NVMCTRL->STATUS.reg |= NVMCTRL_STATUS_MASK;
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WAP;
    while (NVMCTRL->INTFLAG.bit.READY == 0) {
    }

    // Check that the erase really took effect. If some of the flash refused to
    // erase, do not pretend the device is clean: keep the error LED blinking
    // fast and stay here. BOOTPROT and the lock bits have been restored at this
    // point, so a power cycle brings the device back up on the new bootloader.
    for (const volatile uint32_t *p = (const volatile uint32_t *)start_addr;
         (uint32_t)p < FLASH_SIZE; ++p) {
        if (*p != 0xFFFFFFFFu) {
            while (1) {
                LED_ERR_TGL();
                for (volatile uint32_t d = 0; d < 200000u; ++d) {
                }
            }
        }
    }

    LED_ERR_OFF();
    LED_MSC_OFF();

    // Stay in the bootloader after the reset (the app area is erased anyway).
    *DBL_TAP_PTR = DBL_TAP_MAGIC;
    NVIC_SystemReset();

    while (1) {
    }
}

int main(void) {
    led_init();

    logmsg("Start");

    assert((8 << NVMCTRL->PARAM.bit.PSZ) == FLASH_PAGE_SIZE);

    /* We have determined we should stay in the monitor. */
    /* System initialization */
    system_init();
    __disable_irq();
    __DMB();

    logmsg("Before main loop");

    // Disable BOOTPROT while updating the bootloader. This resets and
    // re-enters main() once with BOOTPROT already at 7; everything below then
    // runs exactly once.
    set_fuses_and_bootprot(7); // 0k - See "Table 22-2 Boot Loader Size" in datasheet.

    const uint8_t *ptr = bootloader;
    for (uint32_t i = 0; i < BOOTLOADER_K; ++i) {
        int crc = 0;
        for (uint32_t j = 0; j < 1024; ++j) {
            crc = add_crc(*ptr++, crc);
        }
        if (bootloader_crcs[i] != crc) {
            logmsg("Invalid checksum. Aborting.");
            panic(1);
        }
    }

    for (uint32_t i = 0; i < BOOTLOADER_K * 1024; i += FLASH_ROW_SIZE) {
        memcpy(bootloader_page_buf, &bootloader[i], FLASH_ROW_SIZE);
        flash_write_row((void *)i, (void *)bootloader_page_buf);
    }

    // Verify what actually landed in flash before wiping everything else. If
    // the bootloader area is not intact we must not continue, otherwise the
    // device would be left without any usable code at all.
    {
        volatile uint32_t flash_base = 0; // volatile: keep gcc from folding this into a NULL deref
        const uint8_t *installed = (const uint8_t *)(uintptr_t)flash_base;
        for (uint32_t i = 0; i < BOOTLOADER_K * 1024; ++i) {
            if (installed[i] != bootloader[i]) {
                logmsg("Bootloader verify failed. Aborting.");
                panic(2);
            }
        }
    }

    logmsg("Update successful, wiping flash");

    // Re-base the interrupt vectors to the freshly written bootloader, so that
    // nothing points into the flash we are about to erase.
    SCB->VTOR = 0;

    // Erase everything above the bootloader and reset. Does not return - and
    // cannot return, since it erases the code that called it.
    wipe_flash_and_reset(APP_START_ADDRESS);

    while (1) {
    }
}
