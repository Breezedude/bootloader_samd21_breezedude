// See inc/ota_swap.h for what this does and why. Plain C with four functions
// to the outside, so testing/host/ota_swap runs it against a flash model that
// cuts the power at every single erase and page write.

#include <string.h>
#include "uf2.h"
#include "ota_swap.h"

#define ROW_WORDS (OTA_SWAP_ROW_SIZE / 4u)
#define PAGE_WORDS (OTA_SWAP_PAGE_SIZE / 4u)

enum { DIR_FORWARD = 0, DIR_BACK = 1 };

// ---------------------------------------------------------------- flash rows

static uint32_t read_word(uint32_t address) {
    uint32_t w;
    memcpy(&w, ota_swap_read(address), sizeof(w));
    return w;
}

static bool row_blank(uint32_t address) {
    for (uint32_t i = 0; i < OTA_SWAP_ROW_SIZE; i += 4u) {
        if (read_word(address + i) != 0xFFFFFFFFUL) {
            return false;
        }
    }
    return true;
}

static bool page_blank(uint32_t address) {
    for (uint32_t i = 0; i < OTA_SWAP_PAGE_SIZE; i += 4u) {
        if (read_word(address + i) != 0xFFFFFFFFUL) {
            return false;
        }
    }
    return true;
}

// Checksum of a row (FNV-1a over its words): tells the two copies of a row
// apart after a reset. A bitwise CRC would take seconds over a whole image at
// the 1 MHz the bootloader runs on here.
static uint32_t row_sum(uint32_t address) {
    uint32_t h = 2166136261UL;
    for (uint32_t i = 0; i < OTA_SWAP_ROW_SIZE; i += 4u) {
        h = (h ^ read_word(address + i)) * 16777619UL;
    }
    return h;
}

// Erase and program a row from RAM, unless it already holds this content.
static void write_row(uint32_t address, const uint32_t *words) {
    if (memcmp(ota_swap_read(address), words, OTA_SWAP_ROW_SIZE) == 0) {
        return;
    }
    ota_swap_erase_row(address);
    for (uint32_t p = 0; p < OTA_SWAP_ROW_SIZE / OTA_SWAP_PAGE_SIZE; ++p) {
        ota_swap_write_page(address + p * OTA_SWAP_PAGE_SIZE, words + p * PAGE_WORDS);
    }
}

static void copy_row(uint32_t dst, uint32_t src) {
    uint32_t buf[ROW_WORDS];
    memcpy(buf, ota_swap_read(src), OTA_SWAP_ROW_SIZE);
    write_row(dst, buf);
}

// ------------------------------------------------------------------ flag row

static bool flags_read(OTA_AB_Flags *flags, OTA_SwapExt *ext, bool *ext_ok) {
    memcpy(flags, ota_swap_read(OTA_FLAG_ADDRESS), sizeof(*flags));
    memcpy(ext, ota_swap_read(OTA_FLAG_ADDRESS + OTA_SWAP_EXT_OFFSET), sizeof(*ext));
    const bool ok = ota_ab_valid(flags);
    *ext_ok = ok && ota_swap_ext_valid(ext);
    return ok;
}

// The application's note from the request, handed on in every flag row this
// writes. Lost (0) only when a reset hits the flag row while it is rewritten.
static uint32_t request_tag;

// Rewrites the flag row: nothing pending for the copy scheme, and the swap
// state behind it (state 0: none). This erases the row, and with it the
// journal pages it holds.
static void flags_write(uint32_t state, uint32_t rows) {
    uint32_t row[ROW_WORDS];
    OTA_AB_Flags flags;
    OTA_SwapExt ext;

    memset(row, 0xFF, sizeof(row));
    memset(&flags, 0, sizeof(flags));
    flags.magic = OTA_AB_MAGIC;
    flags.version = OTA_AB_VERSION;
    flags.active_slot = 0;
    flags.pending_slot = 0;
    flags.checksum = ota_ab_checksum(&flags);
    flags.staged_size = rows * OTA_SWAP_ROW_SIZE;
    memcpy(row, &flags, sizeof(flags));

    if (state) {
        ext.magic = OTA_SWAP_MAGIC;
        ext.state = state;
        ext.rows = rows;
        ext.tag = request_tag;
        ext.check = ota_swap_ext_check(&ext);
        memcpy((uint8_t *)row + OTA_SWAP_EXT_OFFSET, &ext, sizeof(ext));
    }
    write_row(OTA_FLAG_ADDRESS, row);
}

// Starts of a firmware on trial are counted without erasing anything: each
// one programs one of the three pages of the flag row that the flags leave
// free. Rewriting the flags for it would put the whole trial at the mercy of
// a reset in that write. A page torn by a reset is not blank and counts.
static uint32_t trial_starts(void) {
    uint32_t n = 1; // the start that did the swap
    for (uint32_t p = 1; p < OTA_SWAP_ROW_SIZE / OTA_SWAP_PAGE_SIZE; ++p) {
        if (!page_blank(OTA_FLAG_ADDRESS + p * OTA_SWAP_PAGE_SIZE)) {
            n++;
        }
    }
    return n;
}

static void trial_count_start(void) {
    uint32_t page[PAGE_WORDS];
    memset(page, 0, sizeof(page)); // all zero: never a valid journal record
    for (uint32_t p = 1; p < OTA_SWAP_ROW_SIZE / OTA_SWAP_PAGE_SIZE; ++p) {
        const uint32_t address = OTA_FLAG_ADDRESS + p * OTA_SWAP_PAGE_SIZE;
        if (page_blank(address)) {
            ota_swap_write_page(address, page);
            return;
        }
    }
}

// ------------------------------------------------------------------- journal

#define JOURNAL_MAGIC 0x4A575300UL // "\0SWJ", low byte: direction
#define JOURNAL_SLOTS 7u
#define JOURNAL_SLOTS_OWN 4u // slots 0..3 in the journal row, 4..6 in the flag row

typedef struct {
    uint32_t magic;
    uint32_t step; // rows << 16 | steps finished before this one; rows << 16 | rows: all done
    uint32_t sum;  // row_sum() of the slot 0 row this step moves
    uint32_t check;
} JournalRecord;

typedef struct {
    bool valid;
    uint32_t dir;
    uint32_t rows;
    uint32_t seq;
    uint32_t sum;
    uint32_t slot;
} JournalState;

static uint32_t slot_address(uint32_t slot) {
    if (slot < JOURNAL_SLOTS_OWN) {
        return OTA_SWAP_JOURNAL_ADDRESS + slot * OTA_SWAP_PAGE_SIZE;
    }
    // page 0 of the flag row holds the flags
    return OTA_FLAG_ADDRESS + (slot - JOURNAL_SLOTS_OWN + 1u) * OTA_SWAP_PAGE_SIZE;
}

static uint32_t record_check(const JournalRecord *r) {
    return ~(r->magic ^ (r->step * 0x01000193UL) ^ r->sum);
}

static bool record_read(uint32_t slot, JournalRecord *r) {
    memcpy(r, ota_swap_read(slot_address(slot)), sizeof(*r));
    if ((r->magic & 0xFFFFFF00UL) != JOURNAL_MAGIC || (r->magic & 0xFFu) > DIR_BACK) {
        return false;
    }
    const uint32_t rows = r->step >> 16;
    const uint32_t seq = r->step & 0xFFFFu;
    return r->check == record_check(r) && rows >= 1u && rows <= OTA_SWAP_MAX_ROWS && seq <= rows;
}

// The newest record. Within one swap the step number only grows, and the
// journal is emptied between swaps.
static void journal_scan(JournalState *j) {
    JournalRecord r;
    memset(j, 0, sizeof(*j));
    for (uint32_t slot = 0; slot < JOURNAL_SLOTS; ++slot) {
        if (!record_read(slot, &r)) {
            continue;
        }
        const uint32_t seq = r.step & 0xFFFFu;
        if (!j->valid || seq >= j->seq) {
            j->valid = true;
            j->dir = r.magic & 0xFFu;
            j->rows = r.step >> 16;
            j->seq = seq;
            j->sum = r.sum;
            j->slot = slot;
        }
    }
}

// What the flag row has to say while a swap in this direction runs.
static void flags_write_running(uint32_t dir, uint32_t rows) {
    flags_write(dir == DIR_FORWARD ? OTA_SWAP_REQUEST : OTA_SWAP_REVERT, rows);
}

// Appends a record. `j` names the newest record so far (or none) and is
// updated. The slots are used in turn; stepping into the first slot of a row
// erases that row, which is safe because the newest record is then in the
// other one. A page that is not blank (a record torn by a reset) is skipped.
static void journal_append(JournalState *j, uint32_t dir, uint32_t rows, uint32_t seq, uint32_t sum) {
    uint32_t page[PAGE_WORDS];
    JournalRecord r;
    uint32_t slot = j->valid ? (j->slot + 1u) % JOURNAL_SLOTS : 0u;

    r.magic = JOURNAL_MAGIC | dir;
    r.step = (rows << 16) | seq;
    r.sum = sum;
    r.check = record_check(&r);
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &r, sizeof(r));

    // Two rounds at most: a full round always passes both row starts.
    for (uint32_t tries = 0; tries < 2u * JOURNAL_SLOTS; ++tries) {
        if (slot == 0u) {
            if (!row_blank(OTA_SWAP_JOURNAL_ADDRESS)) {
                ota_swap_erase_row(OTA_SWAP_JOURNAL_ADDRESS);
            }
        } else if (slot == JOURNAL_SLOTS_OWN) {
            // erases the flag row's journal pages along with the flags, and
            // puts the flags back
            ota_swap_erase_row(OTA_FLAG_ADDRESS);
            flags_write_running(dir, rows);
        }
        if (page_blank(slot_address(slot))) {
            JournalRecord back;
            ota_swap_write_page(slot_address(slot), page);
            if (record_read(slot, &back) && back.step == r.step && back.sum == r.sum) {
                j->valid = true;
                j->dir = dir;
                j->rows = rows;
                j->seq = seq;
                j->sum = sum;
                j->slot = slot;
                return;
            }
        }
        slot = (slot + 1u) % JOURNAL_SLOTS;
    }
}

// ---------------------------------------------------------------------- swap

static uint32_t slot0_row(uint32_t i) { return OTA_SLOT0_START + i * OTA_SWAP_ROW_SIZE; }
static uint32_t slot1_row(uint32_t i) { return OTA_SLOT1_START + i * OTA_SWAP_ROW_SIZE; }

// Runs the swap from where the journal says it stands, to the end.
static void swap_run(uint32_t dir, uint32_t rows, JournalState *j) {
    uint32_t seq = j->valid ? j->seq : 0u;
    bool begun = j->valid; // the step `seq` has a record and may be half done

    while (seq < rows) {
        const uint32_t i = dir == DIR_FORWARD ? seq : rows - 1u - seq;
        const uint32_t own = slot0_row(i);
        const uint32_t hole = slot1_row(dir == DIR_FORWARD ? i : i + 1u);
        const uint32_t next = slot1_row(dir == DIR_FORWARD ? i + 1u : i);
        bool keep_first = true;

        if (begun) {
            // Which copy of the row that was being saved is whole? If slot 0
            // still has it, start the step over. If only the hole has it, the
            // step got past saving it. Neither: the step was complete and the
            // rows happen to match nothing we know - leave them alone.
            if (row_sum(own) == j->sum) {
                keep_first = true;
            } else if (row_sum(hole) == j->sum) {
                keep_first = false;
            } else {
                begun = false;
                seq++;
                continue;
            }
        } else {
            journal_append(j, dir, rows, seq, row_sum(own));
        }

        if (keep_first) {
            copy_row(hole, own);
        }
        copy_row(own, next);
        begun = false;
        seq++;
    }

    if (!(j->valid && j->seq == rows)) {
        journal_append(j, dir, rows, rows, 0);
    }

    // The flag row is about to be rewritten, and while it is erased the
    // journal row alone has to say that the swap is through.
    if (j->slot >= JOURNAL_SLOTS_OWN) {
        ota_swap_erase_row(OTA_SWAP_JOURNAL_ADDRESS);
        j->slot = JOURNAL_SLOTS - 1u; // next slot: 0
        journal_append(j, dir, rows, rows, 0);
    }
    if (dir == DIR_FORWARD) {
        flags_write(OTA_SWAP_TRIAL, rows);
    } else {
        flags_write(OTA_SWAP_REVERTED, rows);
    }
    ota_swap_erase_row(OTA_SWAP_JOURNAL_ADDRESS);
}

void ota_swap_boot(void) {
    OTA_AB_Flags flags;
    OTA_SwapExt ext;
    bool ext_ok = false;
    JournalState j;

    const bool flags_ok = flags_read(&flags, &ext, &ext_ok);
    request_tag = ext_ok ? ext.tag : 0u;
    journal_scan(&j);

    if (!flags_ok) {
        // An erased flag row with a journal next to it: the reset came while
        // the swap was rewriting the flags. The journal knows the rest.
        if (j.valid) {
            // Only the journal row can hold the newest record here, the flag
            // row's pages went with the flags.
            flags_write_running(j.dir, j.rows);
            swap_run(j.dir, j.rows, &j);
        }
        return;
    }

    const uint32_t state = ext_ok ? ext.state : 0u;

    if (state == OTA_SWAP_REQUEST || state == OTA_SWAP_REVERT) {
        const uint32_t dir = state == OTA_SWAP_REQUEST ? DIR_FORWARD : DIR_BACK;
        if (!(j.valid && j.dir == dir && j.rows == ext.rows)) {
            // Not begun. Check what is to be swapped in before touching a row.
            const bool rows_ok = ext.rows >= 1u && ext.rows <= OTA_SWAP_MAX_ROWS;
            const uint32_t image = dir == DIR_FORWARD ? slot1_row(1) : slot1_row(0);
            if (!rows_ok || !ota_swap_image_ok(image)) {
                flags_write(0, 0);
                return;
            }
            memset(&j, 0, sizeof(j));
        }
        swap_run(dir, ext.rows, &j);
        return;
    }

    // The flags name no swap, but the journal may know better: the flag row
    // is rewritten during a swap, and a reset in that write can leave flags
    // that still read as valid without the swap state behind them. A journal
    // that is not through, or one that is through without the flags saying
    // what came of it, is a swap to finish - leaving it would start a slot 0
    // that is half old and half new.
    //
    // A journal that is through and flags that agree with it (on trial after
    // a swap, reverted after going back) is only the leftover of a finished
    // swap, to be erased below.
    if (j.valid) {
        const bool settled = j.seq == j.rows &&
                             ((j.dir == DIR_FORWARD && state == OTA_SWAP_TRIAL) ||
                              (j.dir == DIR_BACK && state == OTA_SWAP_REVERTED));
        if (!settled) {
            swap_run(j.dir, j.rows, &j);
            return;
        }
    }

    // No swap running: a journal row that is not empty is left over from one
    // that finished, with the reset coming before it was erased.
    if (!row_blank(OTA_SWAP_JOURNAL_ADDRESS)) {
        ota_swap_erase_row(OTA_SWAP_JOURNAL_ADDRESS);
    }

    if (state == OTA_SWAP_TRIAL) {
        if (trial_starts() < OTA_SWAP_TRIAL_BOOTS) {
            trial_count_start();
            return;
        }
        // Started this often and never confirmed: back to the old firmware,
        // if it is still where the swap left it. The flags stay as they are
        // until the way back is through - its first journal record is what
        // says that it has begun.
        const bool rows_ok = ext.rows >= 1u && ext.rows <= OTA_SWAP_MAX_ROWS;
        if (!rows_ok || !ota_swap_image_ok(slot1_row(0))) {
            flags_write(0, 0);
            return;
        }
        memset(&j, 0, sizeof(j));
        swap_run(DIR_BACK, ext.rows, &j);
    }
}

void ota_swap_forget(void) {
    OTA_AB_Flags flags;
    OTA_SwapExt ext;
    bool ext_ok = false;

    if (flags_read(&flags, &ext, &ext_ok) && ext_ok) {
        flags_write(0, 0);
    }
}
