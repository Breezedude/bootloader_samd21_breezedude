#include "uf2.h"

#include "sam.h"
#include "bootprot.h"

#if defined(SAMD21)
#define BOOTLOADER_K 16
#elif defined(SAMD51)
#define BOOTLOADER_K 16
#endif

extern const uint8_t bootloader[];
extern const uint16_t bootloader_crcs[];

uint8_t bootloader_page_buf[FLASH_ROW_SIZE];

int main(void) {
    led_init();

    logmsg("Start");

    assert((8 << NVMCTRL->PARAM.bit.PSZ) == FLASH_PAGE_SIZE);
    // assert(FLASH_PAGE_SIZE * NVMCTRL->PARAM.bit.NVMP == FLASH_SIZE);

    /* We have determined we should stay in the monitor. */
    /* System initialization */
    system_init();
    __disable_irq();
    __DMB();

    logmsg("Before main loop");

#ifdef SAMD21
    // Disable BOOTPROT while updating bootloader.
    set_fuses_and_bootprot(7); // 0k - See "Table 22-2 Boot Loader Size" in datasheet.
#endif
#ifdef SAMD51
    // set_fuses_and_bootprot() will cause a reset and not return if
    // the fuses are changed. We'll reenter main() and run this again,
    // and it will do nothing the second time and fall hrough.
    set_fuses_and_bootprot(13); // 16k. See "Table 25-10 Boot Loader Size" in datasheet.

    // We only need to set the BOOTPROT once on the SAMD51. For updates, we can
    // temporarily turn the protection off instead.
    nvmctrl_exec_cmd(NVMCTRL_CTRLB_CMD_SBPDIS);
    // Disable NVM caches, per errata.
    NVMCTRL->CTRLA.bit.CACHEDIS0 = true;
    NVMCTRL->CTRLA.bit.CACHEDIS1 = true;
#endif

#ifdef SAMD21
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
#endif

    for (uint32_t i = 0; i < BOOTLOADER_K * 1024; i += FLASH_ROW_SIZE) {
        memcpy(bootloader_page_buf, &bootloader[i], FLASH_ROW_SIZE);
        flash_write_row((void *)i, (void *)bootloader_page_buf);
    }

    logmsg("Update successful!");

    // re-base int vector back to bootloader, so that the flash erase
    // below doesn't write over the vectors.
    SCB->VTOR = 0;

    // Write zeros to the stack location and reset handler location so the
    // bootloader doesn't run us a second time. We don't need to erase to write
    // zeros. The remainder of the write unit will be set to 1s which should
    // preserve the existing values but its not critical.
    uint32_t zeros[2] = {0, 0};
    flash_write_words((void *)(BOOTLOADER_K * 1024), zeros, 2);

#ifdef SAMD21
    // One-time migration cleanup: scrub the OTA staging area (SLOT1) and the
    // reserved GAP/FLAG/TAIL region back to erased (0xFF), leaving
    // OTA_SETTINGS/OTA_VERSIONS (the device's field configuration, stored at
    // 0x3E000-0x3E400) untouched. Then write a fresh, valid OTA_AB_Flags so
    // every freshly-migrated device starts with active=0/pending=0 and a
    // known-good checksum, instead of inheriting whatever garbage was left
    // over from the old v3.16 layout.
    logmsg("Scrubbing OTA staging/flag area");

    for (uint32_t addr = OTA_SLOT1_START; addr < OTA_FLASH_END; addr += FLASH_ROW_SIZE) {
        flash_erase_row((void *)addr);
    }
    for (uint32_t addr = OTA_VERSIONS_ADDRESS + OTA_VERSIONS_SIZE; addr < FLASH_SIZE; addr += FLASH_ROW_SIZE) {
        flash_erase_row((void *)addr);
    }

    {
        OTA_AB_Flags cfg;
        uint32_t flag_row[FLASH_ROW_SIZE / 4];

        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = OTA_AB_MAGIC;
        cfg.version = OTA_AB_VERSION;
        cfg.active_slot = 0u;
        cfg.pending_slot = 0u;
        cfg.checksum = ota_ab_checksum(&cfg);

        for (uint32_t i = 0; i < (FLASH_ROW_SIZE / 4); ++i) {
            flag_row[i] = 0xFFFFFFFFu;
        }
        memcpy(flag_row, &cfg, sizeof(cfg));
        flash_write_row((void *)OTA_FLAG_ADDRESS, flag_row);
    }

    // Re-enable BOOTPROT
    set_fuses_and_bootprot(1); // 16k
#endif
    // For the SAMD51, the boot protection will automatically be re-enabled on
    // reset.

    LED_MSC_OFF();
    resetIntoBootloader();

    // We should not reach here normally.
    while (1) {
    }
}
