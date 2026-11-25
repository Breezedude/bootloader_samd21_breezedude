// Host test for src/ota_swap.c: the real source against a flash model in which
// the power can fail inside any erase and any page write.
//
// For images of several sizes it runs a swap, a trial that is never
// confirmed, and the swap back, cutting the power at every single flash
// operation in turn - and, for the smaller images, a second time at every
// operation of the recovery. A torn erase leaves a row of old, erased and
// random bytes, a torn page write a page that is partly programmed. After each
// cut the "station" restarts until the bootloader has nothing left to do, and
// then both slots must hold exactly the two images, whole.

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uf2.h"
#include "ota_swap.h"

#define FLASH_BYTES 0x40000u
#define ROW 256u
#define PAGE 64u

static uint8_t flash[FLASH_BYTES];
static jmp_buf power_cut;
static long ops_left = -1; // flash operations until the power fails, -1: never
static long ops_done = 0;
static uint32_t rng_state = 1;
static int failures = 0;
static long cases = 0;

static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static bool cut_now(void) {
    ops_done++;
    if (ops_left < 0) {
        return false;
    }
    if (ops_left == 0) {
        ops_left = -1;
        return true;
    }
    ops_left--;
    return false;
}

const uint8_t *ota_swap_read(uint32_t address) { return &flash[address]; }

void ota_swap_erase_row(uint32_t address) {
    if (address % ROW) {
        printf("FAIL: erase at 0x%05X is not row aligned\n", (unsigned)address);
        failures++;
    }
    if (cut_now()) {
        for (uint32_t i = 0; i < ROW; ++i) {
            switch (rnd() % 3u) {
            case 0: break;                                     // not reached
            case 1: flash[address + i] = 0xFF; break;          // erased
            default: flash[address + i] |= (uint8_t)rnd(); break; // on the way
            }
        }
        longjmp(power_cut, 1);
    }
    memset(&flash[address], 0xFF, ROW);
}

void ota_swap_write_page(uint32_t address, const uint32_t *words) {
    const uint8_t *data = (const uint8_t *)words;
    if (address % PAGE) {
        printf("FAIL: page write at 0x%05X is not page aligned\n", (unsigned)address);
        failures++;
    }
    for (uint32_t i = 0; i < PAGE; ++i) {
        if (flash[address + i] != 0xFF) {
            printf("FAIL: page write at 0x%05X onto a page that is not erased\n", (unsigned)address);
            failures++;
            break;
        }
    }
    if (cut_now()) {
        for (uint32_t i = 0; i < PAGE; ++i) {
            switch (rnd() % 3u) {
            case 0: break;
            case 1: flash[address + i] = data[i]; break;
            default: flash[address + i] = (uint8_t)(data[i] | (uint8_t)rnd()); break;
            }
        }
        longjmp(power_cut, 1);
    }
    memcpy(&flash[address], data, PAGE);
}

bool ota_swap_image_ok(uint32_t address) {
    uint32_t sp, rv;
    memcpy(&sp, &flash[address], 4);
    memcpy(&rv, &flash[address + 4], 4);
    return sp >= 0x20000000UL && sp <= 0x20008000UL && (sp & 3u) == 0 && (rv & 1u) &&
           rv >= OTA_SLOT0_START && rv < OTA_SLOT0_START + OTA_SLOT0_SIZE;
}

// ------------------------------------------------------------------- images

#define MAX_ROWS 40u
static uint8_t img_old[(MAX_ROWS + 2u) * ROW]; // two rows longer than the new one
static uint8_t img_new[MAX_ROWS * ROW];

static void make_images(uint32_t rows, uint32_t seed) {
    rng_state = seed ? seed : 1;
    for (uint32_t i = 0; i < sizeof(img_old); ++i) img_old[i] = (uint8_t)rnd();
    for (uint32_t i = 0; i < sizeof(img_new); ++i) img_new[i] = (uint8_t)rnd();
    // The cases that make "which copy is which" hard:
    if (rows > 3) memcpy(&img_new[2 * ROW], &img_old[2 * ROW], ROW);           // row unchanged by the update
    if (rows > 5) memcpy(&img_old[4 * ROW], &img_new[3 * ROW], ROW);           // old row = new row below it
    if (rows > 6) memcpy(&img_old[6 * ROW], &img_new[5 * ROW], ROW);
    if (rows > 7) memset(&img_new[(rows - 1) * ROW + 32], 0xFF, ROW - 32);     // padded end of the image
    if (rows > 8) memset(&img_old[7 * ROW], 0xFF, ROW);                        // blank row inside
    if (rows > 9) memcpy(&img_new[8 * ROW], &img_new[7 * ROW], ROW);           // two equal neighbours
    const uint32_t sp = 0x20008000UL, rv = OTA_SLOT0_START + 0x101u;
    memcpy(img_old, &sp, 4); memcpy(img_old + 4, &rv, 4);
    memcpy(img_new, &sp, 4); memcpy(img_new + 4, &rv, 4);
}

static void write_request(uint32_t rows) {
    OTA_AB_Flags f;
    OTA_SwapExt e;
    memset(&flash[OTA_FLAG_ADDRESS], 0xFF, ROW);
    memset(&f, 0, sizeof(f));
    f.magic = OTA_AB_MAGIC;
    f.version = OTA_AB_VERSION;
    f.checksum = ota_ab_checksum(&f);
    f.staged_size = rows * ROW - 7u;
    memcpy(&flash[OTA_FLAG_ADDRESS], &f, sizeof(f));
    e.magic = OTA_SWAP_MAGIC;
    e.state = OTA_SWAP_REQUEST;
    e.rows = rows;
    e.tag = 0x124;
    e.check = ota_swap_ext_check(&e);
    memcpy(&flash[OTA_FLAG_ADDRESS + OTA_SWAP_EXT_OFFSET], &e, sizeof(e));
}

// The station as the application leaves it when it asks for the swap.
static void stage(uint32_t rows) {
    memset(flash, 0xFF, sizeof(flash));
    memcpy(&flash[OTA_SLOT0_START], img_old, (rows + 2u) * ROW);
    memcpy(&flash[OTA_SLOT1_START + ROW], img_new, rows * ROW);
    write_request(rows);
}

static uint32_t ext_state(uint32_t *boots) {
    OTA_AB_Flags f;
    OTA_SwapExt e;
    memcpy(&f, &flash[OTA_FLAG_ADDRESS], sizeof(f));
    memcpy(&e, &flash[OTA_FLAG_ADDRESS + OTA_SWAP_EXT_OFFSET], sizeof(e));
    if (!ota_ab_valid(&f) || !ota_swap_ext_valid(&e)) {
        return 0;
    }
    if (boots) *boots = e.tag;
    return e.state;
}

static bool journal_row_blank(void) {
    for (uint32_t i = 0; i < ROW; ++i) {
        if (flash[OTA_SWAP_JOURNAL_ADDRESS + i] != 0xFF) return false;
    }
    return true;
}

// One start of the bootloader; false when the power failed in it.
static bool boot(long cut_after) {
    ops_left = cut_after;
    if (setjmp(power_cut)) {
        return false;
    }
    ota_swap_boot();
    ops_left = -1;
    return true;
}

static void fail(const char *what, uint32_t rows, long c1, long c2) {
    if (failures < 20) {
        printf("FAIL: %s (rows=%u, cut at %ld, then %ld)\n", what, (unsigned)rows, c1, c2);
    }
    failures++;
}

// Slot 0 holds the new image, slot 1 the old one, and what the old image had
// beyond the new one's length is still in slot 0.
static bool is_swapped(uint32_t rows) {
    return memcmp(&flash[OTA_SLOT0_START], img_new, rows * ROW) == 0 &&
           memcmp(&flash[OTA_SLOT0_START + rows * ROW], &img_old[rows * ROW], 2u * ROW) == 0 &&
           memcmp(&flash[OTA_SLOT1_START], img_old, rows * ROW) == 0;
}

// Slot 0 holds the old image again, slot 1 the new one where it was staged.
static bool is_back(uint32_t rows) {
    return memcmp(&flash[OTA_SLOT0_START], img_old, (rows + 2u) * ROW) == 0 &&
           memcmp(&flash[OTA_SLOT1_START + ROW], img_new, rows * ROW) == 0;
}

// Restart until a start goes through, with one more power cut on the way if
// asked for. Returns false if that second cut never came (recovery was shorter).
static bool recover(long second_cut) {
    bool second_hit = false;
    if (second_cut >= 0) {
        second_hit = !boot(second_cut);
    }
    for (int i = 0; i < 4; ++i) {
        if (boot(-1)) break;
    }
    return second_cut < 0 || second_hit;
}

// Forward swap with power cuts. `nested`: also cut every operation of the recovery.
static void test_forward(uint32_t rows, bool nested) {
    for (long c1 = 0;; ++c1) {
        long c2 = -1;
        for (;;) {
            stage(rows);
            rng_state = (uint32_t)(c1 * 7919 + c2 * 104729 + rows) | 1u;
            const bool clean = boot(c1);
            if (!clean) {
                const bool more = recover(c2);
                cases++;
                if (!is_swapped(rows)) fail("forward swap: slots wrong after recovery", rows, c1, c2);
                uint32_t boots = 0;
                if (ext_state(&boots) != OTA_SWAP_TRIAL) fail("forward swap: not on trial after recovery", rows, c1, c2);
                if (!journal_row_blank()) fail("forward swap: journal row left behind", rows, c1, c2);
                if (!nested || !more) break;
                c2++;
                continue;
            }
            // went through without a cut: every operation has been covered
            cases++;
            if (!is_swapped(rows)) fail("forward swap: slots wrong", rows, -1, -1);
            if (ext_state(NULL) != OTA_SWAP_TRIAL) fail("forward swap: not on trial", rows, -1, -1);
            if (!journal_row_blank()) fail("forward swap: journal row left behind", rows, -1, -1);
            return;
        }
    }
}

// A swapped station whose firmware never confirms: after OTA_SWAP_TRIAL_BOOTS
// starts the bootloader goes back, here with power cuts in that.
static void test_revert(uint32_t rows, bool nested) {
    for (long c1 = 0;; ++c1) {
        long c2 = -1;
        for (;;) {
            stage(rows);
            boot(-1);                       // swap, start 1 of the new firmware
            for (uint32_t b = 1; b < OTA_SWAP_TRIAL_BOOTS; ++b) {
                boot(-1);                   // starts 2..n
                if (!is_swapped(rows)) fail("trial: slots changed by a counted start", rows, -1, -1);
            }
            if (ext_state(NULL) != OTA_SWAP_TRIAL) {
                fail("trial: no longer on trial before its starts are used up", rows, -1, -1);
            }
            rng_state = (uint32_t)(c1 * 15485863 + c2 * 32452843 + rows) | 1u;
            const bool clean = boot(c1);    // the start that goes back
            if (!clean) {
                const bool more = recover(c2);
                cases++;
                if (!is_back(rows)) fail("revert: slots wrong after recovery", rows, c1, c2);
                if (ext_state(NULL) != OTA_SWAP_REVERTED) fail("revert: flags do not say reverted", rows, c1, c2);
                if (!journal_row_blank()) fail("revert: journal row left behind", rows, c1, c2);
                if (!nested || !more) break;
                c2++;
                continue;
            }
            cases++;
            if (!is_back(rows)) fail("revert: slots wrong", rows, -1, -1);
            uint32_t tag = 0;
            if (ext_state(&tag) != OTA_SWAP_REVERTED) fail("revert: flags do not say reverted", rows, -1, -1);
            if (tag != 0x124) fail("revert: the application's tag did not survive", rows, -1, -1);
            // and the same image can be asked for again
            write_request(rows);
            boot(-1);
            if (!is_swapped(rows)) fail("second swap after a revert: slots wrong", rows, -1, -1);
            return;
        }
    }
}

// A reset while the swap rewrites the flag row can leave flags that read as
// valid but have lost the swap state. The journal must still carry the swap.
static void test_flags_lose_state(uint32_t rows) {
    for (long c1 = 0;; ++c1) {
        stage(rows);
        rng_state = (uint32_t)(c1 * 2654435761u + rows) | 1u;
        if (boot(c1)) {
            return;
        }
        memset(&flash[OTA_FLAG_ADDRESS + OTA_SWAP_EXT_OFFSET], 0xFF, sizeof(OTA_SwapExt));
        recover(-1);
        cases++;
        // Either the swap had not begun (no journal yet: the request is simply
        // gone and the old firmware stays), or it must have been finished.
        if (!is_swapped(rows) && !is_back(rows)) {
            fail("flags lost their state mid swap: slots are neither old nor new", rows, c1, -1);
        }
    }
}

// The counted starts of a trial rewrite the flag row; a power cut in that must
// never touch the slots.
static void test_trial_count_cut(uint32_t rows) {
    for (long c1 = 0; c1 < 12; ++c1) {
        stage(rows);
        boot(-1);
        rng_state = (uint32_t)(c1 * 40503u + rows) | 1u;
        boot(c1);
        recover(-1);
        cases++;
        if (!is_swapped(rows) && !is_back(rows)) {
            fail("power cut while counting a trial start: slots damaged", rows, c1, -1);
        }
    }
}

// Requests the bootloader must turn down without touching a row.
static void test_refused(void) {
    make_images(8, 99);

    stage(8);
    memset(&flash[OTA_SLOT1_START + ROW], 0x00, 8); // no vector table in the staged image
    boot(-1);
    cases++;
    if (memcmp(&flash[OTA_SLOT0_START], img_old, 10u * ROW) != 0) fail("request without a valid image was acted on", 8, -1, -1);
    if (ext_state(NULL) != 0) fail("refused request is still pending", 8, -1, -1);

    stage(8);
    {
        OTA_SwapExt e;
        e.magic = OTA_SWAP_MAGIC; e.state = OTA_SWAP_REQUEST; e.rows = OTA_SWAP_MAX_ROWS + 1u; e.tag = 0;
        e.check = ota_swap_ext_check(&e);
        memcpy(&flash[OTA_FLAG_ADDRESS + OTA_SWAP_EXT_OFFSET], &e, sizeof(e));
    }
    boot(-1);
    cases++;
    if (memcmp(&flash[OTA_SLOT0_START], img_old, 10u * ROW) != 0) fail("request with too many rows was acted on", 8, -1, -1);

    // a firmware flashed over USB during a trial: no going back to what slot 1 holds
    stage(8);
    boot(-1);
    ota_swap_forget();
    for (int i = 0; i < 6; ++i) boot(-1);
    cases++;
    if (!is_swapped(8)) fail("ota_swap_forget did not stop the trial", 8, -1, -1);
    if (ext_state(NULL) != 0) fail("ota_swap_forget left a swap state", 8, -1, -1);

    // the largest image the layout takes, once, without cuts
    {
        static uint8_t big_old[OTA_SLOT0_SIZE], big_new[OTA_SLOT0_SIZE];
        const uint32_t rows = OTA_SWAP_MAX_ROWS;
        const uint32_t sp = 0x20008000UL, rv = OTA_SLOT0_START + 0x101u;
        rng_state = 4711;
        for (uint32_t i = 0; i < rows * ROW; ++i) { big_old[i] = (uint8_t)rnd(); big_new[i] = (uint8_t)rnd(); }
        memcpy(big_old, &sp, 4); memcpy(big_old + 4, &rv, 4);
        memcpy(big_new, &sp, 4); memcpy(big_new + 4, &rv, 4);
        memset(flash, 0xFF, sizeof(flash));
        memcpy(&flash[OTA_SLOT0_START], big_old, rows * ROW);
        memcpy(&flash[OTA_SLOT1_START + ROW], big_new, rows * ROW);
        write_request(rows);
        ops_done = 0;
        boot(-1);
        cases++;
        if (memcmp(&flash[OTA_SLOT0_START], big_new, rows * ROW) != 0 ||
            memcmp(&flash[OTA_SLOT1_START], big_old, rows * ROW) != 0) {
            fail("full size swap: slots wrong", rows, -1, -1);
        }
        printf("full size swap (%u rows): %ld flash operations\n", (unsigned)rows, ops_done);
        for (uint32_t b = 1; b <= OTA_SWAP_TRIAL_BOOTS; ++b) boot(-1);
        if (memcmp(&flash[OTA_SLOT0_START], big_old, rows * ROW) != 0 ||
            memcmp(&flash[OTA_SLOT1_START + ROW], big_new, rows * ROW) != 0) {
            fail("full size revert: slots wrong", rows, -1, -1);
        }
    }
}

int main(void) {
    static const uint32_t nested_rows[] = {1, 2, 3, 7, 8, 9, 15};
    static const uint32_t single_rows[] = {16, 29, 40};

    for (uint32_t k = 0; k < sizeof(nested_rows) / sizeof(nested_rows[0]); ++k) {
        const uint32_t rows = nested_rows[k];
        make_images(rows, 1000u + rows);
        test_forward(rows, true);
        test_revert(rows, true);
        test_flags_lose_state(rows);
        test_trial_count_cut(rows);
    }
    for (uint32_t k = 0; k < sizeof(single_rows) / sizeof(single_rows[0]); ++k) {
        const uint32_t rows = single_rows[k];
        make_images(rows, 2000u + rows);
        test_forward(rows, false);
        test_revert(rows, false);
        test_flags_lose_state(rows);
    }
    test_refused();

    printf("%ld cases, %d failures\n", cases, failures);
    if (failures) {
        printf("OTA SWAP TEST FAILED\n");
        return 1;
    }
    printf("OTA SWAP TEST PASSED\n");
    return 0;
}
