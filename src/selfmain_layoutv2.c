#include "uf2.h"

#include "sam.h"
#include "bootprot.h"

// One-time "layout v2" migration tool (bootloader v4.2.0+). Installs the new
// 16KB bootloader (same as selfmain.c) and relocates the device's OTA layout
// from v1 (OTA_SLOT1_START=0x20000, OTA_FLASH_END=0x3E000, 8KiB CONFIG tail)
// to v2 (OTA_SLOT1_START=0x21D00, OTA_FLASH_END=0x3FB00, 1280B CONFIG tail),
// as now described by uf2.h. Only OTA_SETTINGS (device configuration) is
// relocated; OTA_VERSIONS is regenerable and OTA_AB_Flags is reset to a known
// state. The running application lives below LAYOUT_V1_OTA_SLOT1_START in
// both layouts and is left untouched.
//
// This is for the handful of field devices already on bootloader v4.1.x
// (16KB bootloader, layout v1). Devices coming straight from v3.16 use
// selfmain.c instead, which lands directly on layout v2 (no v1 layout is
// ever set up for them).

#define BOOTLOADER_K 16

extern const uint8_t bootloader[];
extern const uint16_t bootloader_crcs[];

uint8_t bootloader_page_buf[FLASH_ROW_SIZE];

// Layout v1 locations, superseded by the layout-v2 values in uf2.h. Needed
// here only to read out the device's existing OTA_SETTINGS before the region
// containing them is erased.
#define LAYOUT_V1_OTA_SLOT1_START      0x00020000UL
#define LAYOUT_V1_OTA_SETTINGS_ADDRESS 0x0003E000UL
#define LAYOUT_V1_OTA_SETTINGS_SIZE    0x00000200UL

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
    // re-enters main() once with BOOTPROT already at 7; everything below
    // then runs exactly once.
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

    // Read out the device's current OTA_SETTINGS (station name, sensor type,
    // LoRa configuration, ...) before layout-v2 migration erases the flash
    // region they currently live in. The grown tail is left at its
    // erased-flash value (0xFF).
    uint32_t settings_buf[OTA_SETTINGS_SIZE / 4];
    for (uint32_t i = 0; i < (OTA_SETTINGS_SIZE / 4); ++i) {
        settings_buf[i] = 0xFFFFFFFFu;
    }
    memcpy(settings_buf, (const void *)LAYOUT_V1_OTA_SETTINGS_ADDRESS, LAYOUT_V1_OTA_SETTINGS_SIZE);

    for (uint32_t i = 0; i < BOOTLOADER_K * 1024; i += FLASH_ROW_SIZE) {
        memcpy(bootloader_page_buf, &bootloader[i], FLASH_ROW_SIZE);
        flash_write_row((void *)i, (void *)bootloader_page_buf);
    }

    logmsg("Update successful!");

    // re-base int vector back to bootloader, so that the flash erase
    // below doesn't write over the vectors.
    SCB->VTOR = 0;

    // Write zeros to the stack location and reset handler location so the
    // bootloader doesn't run us a second time.
    uint32_t zeros[2] = {0, 0};
    flash_write_words((void *)(BOOTLOADER_K * 1024), zeros, 2);

    // Layout v2 migration: [LAYOUT_V1_OTA_SLOT1_START, FLASH_SIZE) covers the
    // old OTA_SLOT1 and the old 8KiB CONFIG tail, which together become the
    // new OTA_SLOT0 extension, the new OTA_SLOT1, and the new 1280B CONFIG
    // tail. Erase all of it, then write back the relocated OTA_SETTINGS and a
    // fresh OTA_AB_Flags at their layout-v2 addresses.
    logmsg("Migrating to OTA layout v2");

    flash_erase_to_end((uint32_t *)LAYOUT_V1_OTA_SLOT1_START);

    for (uint32_t i = 0; i < (OTA_SETTINGS_SIZE / FLASH_ROW_SIZE); ++i) {
        flash_write_row((void *)(OTA_SETTINGS_ADDRESS + i * FLASH_ROW_SIZE),
                         &settings_buf[i * (FLASH_ROW_SIZE / 4)]);
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

    LED_MSC_OFF();
    resetIntoBootloader();

    // We should not reach here normally.
    while (1) {
    }
}
