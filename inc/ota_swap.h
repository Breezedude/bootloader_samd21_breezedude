#ifndef OTA_SWAP_H
#define OTA_SWAP_H

// Firmware update by swapping the two slots, with a trial period and a way
// back (bootloader 4.2.2).
//
// Before, a staged image was copied over the running one and the old firmware
// was gone. Now the two are exchanged row by row, so the previous firmware
// stays in the staging slot. The new one runs on trial: it has to confirm
// itself (the application does that once it has seen its radio work and has
// run for a minute). If it is reset OTA_SWAP_TRIAL_BOOTS times without
// confirming, the bootloader swaps the old firmware back.
//
// Layout of a swap request, written by the application:
//
//   slot 1, row 0          free (the first hole)
//   slot 1, rows 1..R      the new image, one row above where the copy scheme
//                          put it
//   slot 1, last row       journal (OTA_SWAP_JOURNAL_ADDRESS), so an image may
//                          have OTA_SWAP_MAX_ROWS rows at most
//   flag row               legacy flags with pending == active, followed by
//                          OTA_SwapExt with state OTA_SWAP_REQUEST
//
// An older bootloader sees nothing pending in that flag row and starts the
// old firmware, so a request it does not understand does no harm. The copy
// scheme (pending_slot == 1, image at row 0, no OTA_SwapExt) still works in
// this bootloader for applications that do not know the swap.
//
// The swap moves the hole down slot 1, one row per step:
//
//   step i:  slot1[i]  <- slot0[i]      old row into the hole
//            slot0[i]  <- slot1[i+1]    new row into place; slot1[i+1] is the
//                                       next hole
//
// and ends with the old image in slot 1 rows 0..R-1. Going back is the same
// walk from the top, which leaves the new image in rows 1..R again.
//
// A reset in the middle must not lose a row. Before each step a journal
// record is written with the step number and a checksum of the row that is
// about to be moved; after a reset the bootloader finds the newest record and
// knows from the checksum which of the two copies of that row is intact, so
// the step is finished or repeated. Journal records are one flash page each:
// four in the journal row, three in the unused pages of the flag row, used in
// turn, so each of the two rows is erased once per seven steps.

#include <stdint.h>
#include <stdbool.h>

#define OTA_SWAP_ROW_SIZE 256u
#define OTA_SWAP_PAGE_SIZE 64u

// Last row of slot 1.
#define OTA_SWAP_JOURNAL_ADDRESS (OTA_SLOT1_START + OTA_SLOT1_SIZE - OTA_SWAP_ROW_SIZE)
// Slot 1 without the journal row and without the hole.
#define OTA_SWAP_MAX_ROWS ((OTA_SLOT1_SIZE / OTA_SWAP_ROW_SIZE) - 2u)

// Resets a new firmware may take before it has confirmed itself. The boot
// that performed the swap counts as the first.
#define OTA_SWAP_TRIAL_BOOTS 3u

#define OTA_SWAP_MAGIC 0x50415753UL // "SWAP"

enum {
    OTA_SWAP_REQUEST = 1,  // application: swap the staged image in
    OTA_SWAP_TRIAL = 2,    // bootloader: new image runs unconfirmed, its starts are counted
    OTA_SWAP_REVERT = 3,   // bootloader: swapping the old image back
    OTA_SWAP_REVERTED = 4, // bootloader: old image is back; the application clears this
};

// Follows OTA_AB_Flags in the flag row (offset OTA_SWAP_EXT_OFFSET). The
// application confirms a trial by writing the flag row without it.
typedef struct {
    uint32_t magic;
    uint32_t state;
    uint32_t rows;  // rows of the swapped image
    uint32_t tag;   // the application's own note (it puts the new version there); carried through every state
    uint32_t check;
} OTA_SwapExt;

#define OTA_SWAP_EXT_OFFSET 24u

static inline uint32_t ota_swap_ext_check(const OTA_SwapExt *e) {
    return ~(e->magic ^ (e->state * 0x01000193UL) ^ (e->rows << 8) ^ (e->tag << 20));
}

static inline bool ota_swap_ext_valid(const OTA_SwapExt *e) {
    return e->magic == OTA_SWAP_MAGIC && e->state >= OTA_SWAP_REQUEST && e->state <= OTA_SWAP_REVERTED &&
           e->check == ota_swap_ext_check(e);
}

// --- what the swap needs from its surroundings (flash_samd21.c / main.c on
// --- the device, a flash model in the host test)

// Flash contents at an address.
const uint8_t *ota_swap_read(uint32_t address);
// Erase one row.
void ota_swap_erase_row(uint32_t address);
// Program one erased page.
void ota_swap_write_page(uint32_t address, const uint32_t *words);
// True when a firmware image linked for slot 0 starts at this address.
bool ota_swap_image_ok(uint32_t address);

// --- what the bootloader calls

// At every start, before anything else looks at the slots: finishes an
// interrupted swap, performs a requested one, counts the starts of a firmware
// on trial and swaps back when it used them up.
void ota_swap_boot(void);

// A firmware arrived by another way (UF2): whatever runs now is not on trial,
// and slot 1 no longer holds its predecessor.
void ota_swap_forget(void);

#endif
