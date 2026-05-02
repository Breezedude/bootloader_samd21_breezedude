#ifndef UF2_H
#define UF2_H 1

#include <stdint.h>
#include "board_config.h"

#include "sam.h"
#define UF2_DEFINE_HANDOVER 1 // for testing
#include "uf2format.h"
#include "uf2hid.h"
#include "main.h"
#include "cdc_enumerate.h"
#include "sam_ba_monitor.h"
#include "usart_sam_ba.h"
#include <stdio.h>
#include <string.h>


#include "configkeys.h"


#undef DISABLE
#undef ENABLE

// always go for crystalless - smaller and more compatible
#ifndef CRYSTALLESS
#define CRYSTALLESS 1
#endif

#ifndef USB_PID
#define USB_VID 0x03EB // Atmel
#define USB_PID 0x2402 // Generic HID device
#endif

#ifndef INDEX_URL
#define INDEX_URL "https://www.pxt.io/"
#endif

#include "uf2_version.h"

// Single-image OTA layout for Breezedude on SAMD21.
// - 16 KiB UF2 bootloader at 0x00000000..0x00003FFF
// - runtime bank at 0x00004000..0x0001FFFF
// - staging bank at 0x00020000..0x0003DFFF
// - top 8 KiB reserved for bootloader-managed settings/version text blobs and flags
#ifndef APP_START_ADDRESS
#define APP_START_ADDRESS 0x00004000UL
#endif
#ifndef OTA_SLOT0_START
#define OTA_SLOT0_START APP_START_ADDRESS
#endif
#ifndef OTA_SLOT1_START
#define OTA_SLOT1_START 0x00020000UL
#endif
#ifndef OTA_FLASH_END
#define OTA_FLASH_END 0x0003E000UL
#endif
#ifndef OTA_SLOT0_SIZE
#define OTA_SLOT0_SIZE (OTA_SLOT1_START - OTA_SLOT0_START)
#endif
#ifndef OTA_SLOT1_SIZE
#define OTA_SLOT1_SIZE (OTA_FLASH_END - OTA_SLOT1_START)
#endif
#ifndef OTA_SETTINGS_ADDRESS
#define OTA_SETTINGS_ADDRESS 0x0003E000UL
#endif
#ifndef OTA_SETTINGS_SIZE
#define OTA_SETTINGS_SIZE 0x00000200UL
#endif
#ifndef OTA_VERSIONS_ADDRESS
#define OTA_VERSIONS_ADDRESS 0x0003E200UL
#endif
#ifndef OTA_VERSIONS_SIZE
#define OTA_VERSIONS_SIZE 0x00000200UL
#endif
#ifndef OTA_FLAG_ADDRESS
#define OTA_FLAG_ADDRESS 0x0003EF00UL
#endif
#define OTA_AB_MAGIC 0x42544455UL
#define OTA_AB_VERSION 1UL
#define OTA_AB_NUM_SLOTS 2UL

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t checksum;
} OTA_AB_Flags;

static inline uint32_t ota_ab_checksum_words(uint32_t active_slot, uint32_t pending_slot) {
    return OTA_AB_MAGIC ^ OTA_AB_VERSION ^ active_slot ^ pending_slot ^ 0xA5A55A5AUL;
}

static inline uint32_t ota_ab_checksum(const OTA_AB_Flags *cfg) {
    return ota_ab_checksum_words(cfg->active_slot, cfg->pending_slot);
}

static inline bool ota_ab_valid(const OTA_AB_Flags *cfg) {
    return cfg->magic == OTA_AB_MAGIC && cfg->version == OTA_AB_VERSION &&
           cfg->active_slot < OTA_AB_NUM_SLOTS && cfg->pending_slot < OTA_AB_NUM_SLOTS &&
           cfg->checksum == ota_ab_checksum(cfg);
}

static inline uint32_t ota_slot_address(uint32_t slot) {
    return slot ? OTA_SLOT1_START : OTA_SLOT0_START;
}

// needs to be more than ~4200 (to force FAT16)
#define NUM_FAT_BLOCKS 16000

// Logging to help debugging
#define USE_LOGS 0
// Check various conditions; best leave on
#define USE_ASSERT 0 // 188 bytes
// Enable reading flash via FAT files; otherwise drive will appear empty
#define USE_FAT 1 // 272 bytes
// Enable index.htm file on the drive
#define USE_INDEX_HTM 0 // 132 bytes; disabled to save space, WebUSB stays enabled separately
// Enable USB CDC (Communication Device Class; i.e., USB serial) monitor for Arduino style flashing
#define USE_CDC 1 // 1264 bytes (plus terminal, see below)
// Support the UART (real serial port, not USB)
#define USE_UART 0
// Support Human Interface Device (HID) - serial, flashing and debug
#define USE_HID 0 // 788 bytes; disabled to free space
// Expose HID-like transport via WebUSB
#define USE_WEBUSB 1
// Doesn't yet disable code, just enumeration
#define USE_MSC 1

#ifdef BOARD_SCREEN
#define USE_SCREEN 1
#else
#define USE_SCREEN 0
#endif

// If enabled, bootloader will start on power-on and every reset. A second reset
// will start the app. This only happens if the app says it wants that (see SINGLE_RESET() below).
// If enabled here or by the app, the bootloader will try a temporary monitor stay on normal
// reset. On Breezedude this can keep the device in the bootloader whenever USB enumerates,
// so keep it off and use the normal double-click / explicit handover path instead.
#define USE_SINGLE_RESET 0

// Fine-tuning of features
#define USE_HID_SERIAL 0   // just an example, not really needed; 36 bytes
#define USE_HID_EXT 0      // extended HID commands (read/write mem); 60 bytes; disabled
#define USE_HID_HANDOVER 0 // allow HID application->bootloader seamless transition; 56 bytes; disabled
#define USE_MSC_HANDOVER 0 // ditto for MSC; 348 bytes; disabled for space
#define USE_MSC_CHECKS 0   // check validity of MSC commands; 460 bytes
#define USE_CDC_TERMINAL 0 // enable ASCII mode on CDC loop (not used by BOSSA); 228 bytes
#define USE_DBG_MSC 0      // output debug info about MSC

#if USE_CDC
#define CDC_VERSION "S"
#else
#define CDC_VERSION ""
#endif

#if USE_LOGS
#define LOGS_VERSION "L"
#else
#define LOGS_VERSION ""
#endif

#if USE_FAT
#define FAT_VERSION "F"
#else
#define FAT_VERSION ""
#endif

#if USE_ASSERT
#define ASSERT_VERSION "A"
#else
#define ASSERT_VERSION ""
#endif

#if USE_HID
#define HID_VERSION "H"
#else
#define HID_VERSION ""
#endif

#if USE_SINGLE_RESET
#define RESET_VERSION "R"
#else
#define RESET_VERSION ""
#endif

#if USE_WEBUSB
#define WEB_VERSION "W"
#else
#define WEB_VERSION ""
#endif

#if USE_MSC_HANDOVER
#define MSC_HANDOVER_VERSION "O"
#else
#define MSC_HANDOVER_VERSION ""
#endif

#ifndef UF2_VERSION_PUBLIC
#define UF2_VERSION_PUBLIC UF2_VERSION_BASE
#endif

#ifdef UF2_VERSION_OVERRIDE
#define UF2_VERSION UF2_VERSION_OVERRIDE
#else
#define UF2_VERSION                                                                                \
    UF2_VERSION_BASE " " CDC_VERSION LOGS_VERSION FAT_VERSION ASSERT_VERSION HID_VERSION           \
        WEB_VERSION RESET_VERSION MSC_HANDOVER_VERSION
#endif

// End of config

#define USE_MONITOR (USE_CDC || USE_UART)

#ifdef SAMD51
// 51 also runs at 48MHz in bootloader mode, but it's still faster
#define TIMER_STEP 2000
#else
#define TIMER_STEP 1500
#endif

#ifdef BOARD_NEOPIXEL_PIN
#define COLOR_START 0x040000
#define COLOR_USB 0x000400
#define COLOR_UART 0x040400
#define COLOR_LEAVE 0x000000
#else
#define COLOR_START 0x000040
#define COLOR_USB 0x004000
#define COLOR_UART 0x404000
#define COLOR_LEAVE 0x000000
#endif

/*
From CPU config:
#define FLASH_SIZE            0x8000UL
#define FLASH_PAGE_SIZE       64
#define FLASH_NB_OF_PAGES     512
*/

// These two need to be defined as plain decimal numbers, as we're using # on them
#define FLASH_ROW_SIZE 256
#ifndef FLASH_NUM_ROWS
#define FLASH_NUM_ROWS 1024
#endif

#define NOOP                                                                                       \
    do {                                                                                           \
    } while (0)

#if USE_LOGS
struct LogStore {
    int ptr;
    char buffer[4096];
};
extern struct LogStore logStoreUF2;
void logmsg(const char *msg);
void logval(const char *lbl, uint32_t v);
void logwritenum(uint32_t n);
void logwrite(const char *msg);
void logreset(void);
#else
#define logmsg(...) NOOP
#define logval(...) NOOP
#define logwritenum(...) NOOP
#define logwrite(...) NOOP
#define logreset() NOOP
#endif

#if USE_DBG_MSC
#define DBG_MSC(x) x
#else
#define DBG_MSC(x) NOOP
#endif

void panic(int code);

#if USE_ASSERT
#define assert(cond)                                                                               \
    if (!(cond)) {                                                                                 \
        panic(__LINE__);                                                                           \
    }
#else
#define assert(cond) NOOP
#endif

extern volatile bool b_sam_ba_interface_usart;
void flash_write_row(uint32_t *dst, uint32_t *src);
void flash_erase_to_end(uint32_t *start_address);
void flash_write_words(uint32_t *dst, uint32_t *src, uint32_t n_words);
void copy_words(uint32_t *dst, uint32_t *src, uint32_t n_words);

int writeNum(char *buf, uint32_t n, bool full);

void process_hid(void);

// index of highest LUN
#define MAX_LUN 0
void process_msc(void);
void msc_reset(void);
//! Static block size for all memories
#define UDI_MSC_BLOCK_SIZE 512L

void read_block(uint32_t block_no, uint8_t *data);
#define MAX_BLOCKS (FLASH_SIZE / 256 + 100)
typedef struct {
    uint32_t numBlocks;
    uint32_t numWritten;
    uint8_t writtenMask[MAX_BLOCKS / 8 + 1];
} WriteState;
void write_block(uint32_t block_no, uint8_t *data, bool quiet, WriteState *state);
void padded_memcpy(char *dst, const char *src, int len);

// Last word in RAM
// Unlike for ordinary applications, our link script doesn't place the stack at the bottom
// of the RAM, but instead after all allocated BSS.
// In other words, this word should survive reset.
#ifdef SAMD21
#define DBL_TAP_PTR ((volatile uint32_t *)(HMCRAMC0_ADDR + HMCRAMC0_SIZE - 4))
#endif
#ifdef SAMD51
#define DBL_TAP_PTR ((volatile uint32_t *)(HSRAM_ADDR + HSRAM_SIZE - 4))
#endif
#define DBL_TAP_MAGIC 0xf01669ef // Randomly selected, adjusted to have first and last bit set
#define DBL_TAP_MAGIC_QUICK_BOOT 0xf02669ef

#if USE_SINGLE_RESET
#ifdef SAMD21
#define SINGLE_RESET() (*((uint32_t *)0x20B4) == 0x87eeb07c)
#endif
#ifdef SAMD51
#define SINGLE_RESET() (*((uint32_t *)0x4268) == 0x87eeb07c)
#endif
#endif

void resetIntoApp(void);
void resetIntoBootloader(void);
extern uint32_t current_cpu_frequency_MHz;
extern volatile bool led_tick_on;
void system_init(void);

#define LED_TICK led_tick

#define PINOP(pin, OP) (PORT->Group[(pin) / 32].OP.reg = (1 << ((pin) % 32)))

void led_tick(void);
void led_signal(void);
void led_init(void);
void RGBLED_set_color(uint32_t color);

// Not all targets have a LED
#if defined(LED_PIN)
#if !defined(LED_PIN_PULLUP)
#define LED_MSC_OFF() PINOP(LED_PIN, OUTCLR)
#define LED_MSC_ON() PINOP(LED_PIN, OUTSET)
#else
#define LED_MSC_OFF() PINOP(LED_PIN, OUTSET)
#define LED_MSC_ON() PINOP(LED_PIN, OUTCLR)
#endif
#define LED_MSC_TGL() PINOP(LED_PIN, OUTTGL)
#else
#define LED_MSC_OFF()
#define LED_MSC_ON()
#define LED_MSC_TGL()
#endif

// Not all targets have a dedicated flash/error LED
#if defined(ERROR_LED_PIN)
#if defined(ERROR_LED_PIN_PULLUP)
#define LED_ERR_OFF() PINOP(ERROR_LED_PIN, OUTSET)
#define LED_ERR_ON() PINOP(ERROR_LED_PIN, OUTCLR)
#else
#define LED_ERR_OFF() PINOP(ERROR_LED_PIN, OUTCLR)
#define LED_ERR_ON() PINOP(ERROR_LED_PIN, OUTSET)
#endif
#define LED_ERR_TGL() PINOP(ERROR_LED_PIN, OUTTGL)
#else
#define LED_ERR_OFF()
#define LED_ERR_ON()
#define LED_ERR_TGL()
#endif

// Not all targets have a TX LED
#if defined(LED_TX_PIN)
#if defined(LED_TX_PIN_PULLUP)
#define LED_TX_OFF() PINOP(LED_TX_PIN, OUTSET)
#define LED_TX_ON() PINOP(LED_TX_PIN, OUTCLR)
#else
#define LED_TX_OFF() PINOP(LED_TX_PIN, OUTCLR)
#define LED_TX_ON() PINOP(LED_TX_PIN, OUTSET)
#endif
#define LED_TX_TGL() PINOP(LED_TX_PIN, OUTTGL)
#else
#define LED_TX_OFF()
#define LED_TX_ON()
#define LED_TX_TGL()
#endif

// Not all targets have a RX LED
#if defined(LED_RX_PIN)
#if defined(LED_RX_PIN_PULLUP)
#define LED_RX_OFF() PINOP(LED_RX_PIN, OUTSET)
#define LED_RX_ON() PINOP(LED_RX_PIN, OUTCLR)
#else
#define LED_RX_OFF() PINOP(LED_RX_PIN, OUTCLR)
#define LED_RX_ON() PINOP(LED_RX_PIN, OUTSET)
#endif
#define LED_RX_TGL() PINOP(LED_RX_PIN, OUTTGL)
#else
#define LED_RX_OFF()
#define LED_RX_ON()
#define LED_RX_TGL()
#endif

extern uint32_t timerHigh, resetHorizon;
void timerTick(void);
void delay(uint32_t ms);
void hidHandoverLoop(int ep);
void handoverPrep(void);

// Useful for debugging.
#ifdef BLINK_DEBUG
void blink_n(uint32_t pin, uint32_t n, uint32_t interval);
void blink_n_forever(uint32_t pin, uint32_t n, uint32_t interval);
#endif

#define CONCAT_1(a, b) a##b
#define CONCAT_0(a, b) CONCAT_1(a, b)
#define STATIC_ASSERT(e) enum { CONCAT_0(_static_assert_, __LINE__) = 1 / ((e) ? 1 : 0) }

#ifdef SAMD21
STATIC_ASSERT(FLASH_ROW_SIZE == FLASH_PAGE_SIZE * 4);
STATIC_ASSERT(FLASH_ROW_SIZE == NVMCTRL_ROW_SIZE);
STATIC_ASSERT(FLASH_NUM_ROWS * 4 == FLASH_NB_OF_PAGES);
#endif

extern const char uf2VersionString[];

#if USE_INFO_UF2
extern char infoUf2File[];
#endif

#if USE_SCREEN
void draw_screen(void);
void draw_hf2(void);
void draw_drag(void);
void screen_init(void);
void screen_early_init(void);
#endif

#endif
