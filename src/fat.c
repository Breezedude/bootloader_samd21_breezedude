
#include "uf2.h"

#define SERIAL0 (*(uint32_t *)0x0080A00C)
#define SERIAL1 (*(uint32_t *)0x0080A040)
#define SERIAL2 (*(uint32_t *)0x0080A044)
#define SERIAL3 (*(uint32_t *)0x0080A048)

typedef struct {
    uint8_t JumpInstruction[3];
    uint8_t OEMInfo[8];
    uint16_t SectorSize;
    uint8_t SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t FATCopies;
    uint16_t RootDirectoryEntries;
    uint16_t TotalSectors16;
    uint8_t MediaDescriptor;
    uint16_t SectorsPerFAT;
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t TotalSectors32;
    uint8_t PhysicalDriveNum;
    uint8_t Reserved;
    uint8_t ExtendedBootSig;
    uint32_t VolumeSerialNumber;
    uint8_t VolumeLabel[11];
    uint8_t FilesystemIdentifier[8];
} __attribute__((packed)) FAT_BootBlock;

typedef struct {
    char name[8];
    char ext[3];
    uint8_t attrs;
    uint8_t reserved;
    uint8_t createTimeFine;
    uint16_t createTime;
    uint16_t createDate;
    uint16_t lastAccessDate;
    uint16_t highStartCluster;
    uint16_t updateTime;
    uint16_t updateDate;
    uint16_t startCluster;
    uint32_t size;
} __attribute__((packed)) DirEntry;

STATIC_ASSERT(sizeof(DirEntry) == 32);

struct TextFile {
    const char name[11];
    const char *content;
    uint32_t flashAddr;
    uint32_t maxSize;
};

#define STR0(x) #x
#define STR(x) STR0(x)

/* Version string always present in flash so the app can locate it by scanning
 * bootloader space for the "UF2 Bootloader " prefix, regardless of USE_INFO_UF2. */
const char uf2VersionString[] = "UF2 Bootloader " UF2_VERSION "\r\n";

#if USE_INFO_UF2
char infoUf2File[160];

static void uint32_to_hex(uint32_t v, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; --i) {
        out[i] = hex[v & 0xF];
        v >>= 4;
    }
}

static char *append_str(char *dst, const char *end, const char *src) {
    while (*src && dst < end) {
        *dst++ = *src++;
    }
    return dst;
}

static void build_info_file(void) {
    if (infoUf2File[0] != '\0') {
        return; // already built
    }
    char uuid[33];
    uint32_to_hex(SERIAL0, uuid +  0);
    uint32_to_hex(SERIAL1, uuid +  8);
    uint32_to_hex(SERIAL2, uuid + 16);
    uint32_to_hex(SERIAL3, uuid + 24);
    uuid[32] = '\0';

    char *p = infoUf2File;
    char *e = infoUf2File + sizeof(infoUf2File) - 1;
    p = append_str(p, e, "UF2 Bootloader " UF2_VERSION "\r\n");
    p = append_str(p, e, "Model: " PRODUCT_NAME "\r\n");
    p = append_str(p, e, "Board-ID: " BOARD_ID "\r\n");
    p = append_str(p, e, "Chip-ID: ");
    p = append_str(p, e, uuid);
    p = append_str(p, e, "\r\n");
    *p = '\0';
}
#endif // USE_INFO_UF2

static const char defaultSettingsFile[] =
    "# Replace this file contents with the actual settings file. To create one it is recommended to use the configurator at https://install.breezedude.de/\r\n";

static const char defaultVersionsFile[] =
    "Bootloader: " UF2_VERSION_PUBLIC "\r\n"
    "Bootloader build date: " __DATE__ " " __TIME__ "\r\n";

static void text_file_write(const struct TextFile *file, const uint8_t *data, uint32_t len);

static bool version_line_matches_installed(const uint8_t *src, uint32_t maxSize) {
    static const char prefix[] = "Bootloader: ";
    static const char version[] = UF2_VERSION_PUBLIC;
    const uint32_t prefixLen = sizeof(prefix) - 1;
    const uint32_t versionLen = sizeof(version) - 1;

    for (uint32_t i = 0; i < maxSize; ++i) {
        uint8_t c = src[i];
        if (c == 0x00 || c == 0xFF) {
            break;
        }
        if (i > 0 && src[i - 1] != '\n' && src[i - 1] != '\r') {
            continue;
        }
        if (i + prefixLen + versionLen > maxSize) {
            break;
        }
        if (memcmp(src + i, prefix, prefixLen) != 0) {
            continue;
        }

        uint32_t pos = i + prefixLen;
        if (memcmp(src + pos, version, versionLen) != 0) {
            continue;
        }
        pos += versionLen;

        if (pos >= maxSize) {
            return true;
        }

        c = src[pos];
        if (c == '\r' || c == '\n' || c == 0x00 || c == 0xFF) {
            return true;
        }
    }

    return false;
}

static void ensure_versions_file_matches_bootloader(const struct TextFile *file) {
    if (file->flashAddr != OTA_VERSIONS_ADDRESS || !file->flashAddr || !file->maxSize) {
        return;
    }

    const uint8_t *src = (const uint8_t *)(uintptr_t)file->flashAddr;
    if (version_line_matches_installed(src, file->maxSize)) {
        return;
    }

    text_file_write(file, (const uint8_t *)defaultVersionsFile, strlen(defaultVersionsFile));
}

#if USE_FAT
#if USE_INDEX_HTM
const char indexFile[] = //
    "<!doctype html>\n"
    "<html>"
    "<body>"
    "<script>\n"
    "location.replace(\"" INDEX_URL "\");\n"
    "</script>"
    "</body>"
    "</html>\n";
#endif

// WARNING -- code presumes only one NULL .content for .UF2 file
//            and requires it be the last element of the array
static const struct TextFile info[] = {
#if USE_INFO_UF2
    {.name = "INFO_UF2TXT", .content = infoUf2File, .flashAddr = 0, .maxSize = 0},
#endif
#if USE_INDEX_HTM
    {.name = "INDEX   HTM", .content = indexFile, .flashAddr = 0, .maxSize = 0},
#endif
    {.name = "SETTINGSTXT", .content = NULL, .flashAddr = OTA_SETTINGS_ADDRESS, .maxSize = OTA_SETTINGS_SIZE},
    {.name = "VERSIONSTXT", .content = NULL, .flashAddr = OTA_VERSIONS_ADDRESS, .maxSize = OTA_VERSIONS_SIZE},
    {.name = "CURRENT UF2", .content = NULL, .flashAddr = 0, .maxSize = 0},
};
#define NUM_FILES (sizeof(info) / sizeof(info[0]))
#define NUM_DIRENTRIES (NUM_FILES + 1) // Code adds volume label as first root directory entry

#define UF2_SIZE (FLASH_SIZE * 2)
#define UF2_SECTORS (UF2_SIZE / 512)
#define UF2_FIRST_SECTOR (NUM_FILES + 1) // WARNING -- code presumes each non-UF2 file content fits in single sector
#define UF2_LAST_SECTOR (UF2_FIRST_SECTOR + UF2_SECTORS - 1)
#endif

#define RESERVED_SECTORS 1
#define ROOT_DIR_SECTORS 4
#define SECTORS_PER_FAT ((NUM_FAT_BLOCKS * 2 + 511) / 512)

#define START_FAT0 RESERVED_SECTORS
#define START_FAT1 (START_FAT0 + SECTORS_PER_FAT)
#define START_ROOTDIR (START_FAT1 + SECTORS_PER_FAT)
#define START_CLUSTERS (START_ROOTDIR + ROOT_DIR_SECTORS)

// all directory entries must fit in a single sector
// because otherwise current code overflows buffer
#define DIRENTRIES_PER_SECTOR (512/sizeof(DirEntry))
#if USE_FAT
STATIC_ASSERT(NUM_DIRENTRIES < DIRENTRIES_PER_SECTOR * ROOT_DIR_SECTORS);
#endif

static const FAT_BootBlock BootBlock = {
    .JumpInstruction = {0xeb, 0x3c, 0x90},
    .OEMInfo = "UF2 UF2 ",
    .SectorSize = 512,
    .SectorsPerCluster = 1,
    .ReservedSectors = RESERVED_SECTORS,
    .FATCopies = 2,
    .RootDirectoryEntries = (ROOT_DIR_SECTORS * DIRENTRIES_PER_SECTOR),
    .TotalSectors16 = NUM_FAT_BLOCKS - 2,
    .MediaDescriptor = 0xF8,
    .SectorsPerFAT = SECTORS_PER_FAT,
    .SectorsPerTrack = 1,
    .Heads = 1,
    .PhysicalDriveNum = 0x80, // to match MediaDescriptor of 0xF8
    .ExtendedBootSig = 0x29,
    .VolumeSerialNumber = 0x00420042,
    .VolumeLabel = VOLUME_LABEL,
    .FilesystemIdentifier = "FAT16   ",
};

void padded_memcpy(char *dst, const char *src, int len) {
    for (int i = 0; i < len; ++i) {
        if (*src)
            *dst = *src++;
        else
            *dst = ' ';
        dst++;
    }
}

static const char *text_file_fallback(const struct TextFile *file) {
    if (file->flashAddr == OTA_SETTINGS_ADDRESS) {
        return defaultSettingsFile;
    }
    if (file->flashAddr == OTA_VERSIONS_ADDRESS) {
        return defaultVersionsFile;
    }
    return file->content;
}

static uint32_t text_file_read(const struct TextFile *file, uint8_t *data) {
    memset(data, 0, 512);

    if (file->content) {
        uint32_t len = strlen(file->content);
        memcpy(data, file->content, len > 511 ? 511 : len);
        return len;
    }

    if (file->flashAddr && file->maxSize) {
        ensure_versions_file_matches_bootloader(file);

        const uint8_t *src = (const uint8_t *)(uintptr_t)file->flashAddr;
        uint32_t n = 0;
        bool any = false;

        while (n < file->maxSize && n < 511) {
            uint8_t c = src[n];
            if (c == 0x00 || c == 0xFF) {
                break;
            }
            data[n] = c;
            any = true;
            n++;
        }

        if (any) {
            return n;
        }
    }

    const char *fallback = text_file_fallback(file);
    if (!fallback) {
        return 0;
    }

    uint32_t len = strlen(fallback);
    memcpy(data, fallback, len > 511 ? 511 : len);
    return len;
}

static uint32_t text_file_size(const struct TextFile *file) {
    if (file->content) {
        return strlen(file->content);
    }
    if (file->flashAddr && file->maxSize) {
        uint8_t tmp[512];
        return text_file_read(file, tmp);
    }
    return UF2_SIZE;
}

static void ota_force_slot0_active(void) {
    OTA_AB_Flags cfg;
    uint32_t row[FLASH_ROW_SIZE / 4];

    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = OTA_AB_MAGIC;
    cfg.version = OTA_AB_VERSION;
    cfg.active_slot = 0u;
    cfg.pending_slot = 0u;
    cfg.checksum = ota_ab_checksum(&cfg);

    for (uint32_t i = 0; i < (FLASH_ROW_SIZE / 4); ++i) {
        row[i] = 0xFFFFFFFFu;
    }
    memcpy(row, &cfg, sizeof(cfg));
    flash_write_row((uint32_t *)(uintptr_t)OTA_FLAG_ADDRESS, row);
}

static void text_file_write(const struct TextFile *file, const uint8_t *data, uint32_t len) {
    uint8_t row[FLASH_ROW_SIZE];

    if (!file->flashAddr || !file->maxSize) {
        return;
    }

    for (uint32_t off = 0; off < file->maxSize; off += FLASH_ROW_SIZE) {
        uint32_t chunk = file->maxSize - off;
        if (chunk > FLASH_ROW_SIZE) {
            chunk = FLASH_ROW_SIZE;
        }

        memset(row, 0xFF, sizeof(row));
        if (off < len) {
            uint32_t copyLen = len - off;
            if (copyLen > chunk) {
                copyLen = chunk;
            }
            memcpy(row, data + off, copyLen);
        }

        flash_write_row((uint32_t *)(uintptr_t)(file->flashAddr + off), (uint32_t *)(void *)row);
    }
}

void read_block(uint32_t block_no, uint8_t *data) {
#if USE_INFO_UF2
    build_info_file();
#endif
    memset(data, 0, 512);
    uint32_t sectionIdx = block_no;

    if (block_no == 0) { // Requested boot block
        memcpy(data, &BootBlock, sizeof(BootBlock));
        data[510] = 0x55;
        data[511] = 0xaa;
        // logval("data[0]", data[0]);
    } else if (block_no < START_ROOTDIR) {  // Requested FAT table sector
        sectionIdx -= START_FAT0;
        // logval("sidx", sectionIdx);
        if (sectionIdx >= SECTORS_PER_FAT)
            sectionIdx -= SECTORS_PER_FAT; // second FAT is same as the first...
#if USE_FAT
        if (sectionIdx == 0) {
            data[0] = 0xf0;
            // WARNING -- code presumes only one NULL .content for .UF2 file
            //            and all non-NULL .content fit in one sector
            //            and requires it be the last element of the array
            for (int i = 1; i < NUM_FILES * 2 + 4; ++i) {
                data[i] = 0xff;
            }
        }
        for (int i = 0; i < 256; ++i) { // Generate the FAT chain for the firmware "file"
            uint32_t v = sectionIdx * 256 + i;
            if (UF2_FIRST_SECTOR <= v && v <= UF2_LAST_SECTOR)
                ((uint16_t *)(void *)data)[i] = v == UF2_LAST_SECTOR ? 0xffff : v + 1;
        }
#else
        if (sectionIdx == 0)
            memcpy(data, "\xf0\xff\xff\xff", 4);
#endif
    }
#if USE_FAT
    else if (block_no < START_CLUSTERS) { // Requested sector of the root directory
        sectionIdx -= START_ROOTDIR;
        if (sectionIdx == 0) {
            DirEntry *d = (void *)data;
            padded_memcpy(d->name, BootBlock.VolumeLabel, 11);
            d->attrs = 0x28;
            for (int i = 0; i < NUM_FILES; ++i) {
                d++;
                const struct TextFile *inf = &info[i];
                d->size = text_file_size(inf);
                d->startCluster = i + 2;
                padded_memcpy(d->name, inf->name, 11);
                d->createDate = 0x4d99;
                d->updateDate = 0x4d99;
            }
        }
    } else { // Requested sector from file space
        sectionIdx -= START_CLUSTERS;
        // WARNING -- code presumes all but last file take exactly one sector
        if (sectionIdx < NUM_FILES - 1) {
            text_file_read(&info[sectionIdx], data);
        } else {
            sectionIdx -= NUM_FILES - 1;
            uint32_t addr = sectionIdx * 256;
            if (addr < FLASH_SIZE) {
                UF2_Block *bl = (void *)data;
                bl->magicStart0 = UF2_MAGIC_START0;
                bl->magicStart1 = UF2_MAGIC_START1;
                bl->magicEnd = UF2_MAGIC_END;
                bl->blockNo = sectionIdx;
                bl->numBlocks = FLASH_SIZE / 256;
                bl->targetAddr = addr;
                bl->payloadSize = 256;
                bl->flags |= UF2_FLAG_FAMILYID_PRESENT;
                bl->familyID = UF2_FAMILY;
                memcpy(bl->data, (void *)addr, bl->payloadSize);
            }
        }
    }
#endif
}

void write_block(uint32_t block_no, uint8_t *data, bool quiet, WriteState *state) {
    bool valid_target = false;
#if USE_FAT
    if (block_no >= START_CLUSTERS) {
        uint32_t sectionIdx = block_no - START_CLUSTERS;
        if (sectionIdx < NUM_FILES - 1) {
            const struct TextFile *file = &info[sectionIdx];
            if (file->flashAddr && file->maxSize) {
                text_file_write(file, data, 512);
                // Keep the USB MSC connection alive for editable virtual text files.
                // A reset is useful after flashing a UF2 firmware image, but it is
                // disruptive when the host simply saves SETTINGS.TXT or VERSIONS.TXT.
                return;
            }
        }
    }
#endif

    UF2_Block *bl = (void *)data;
    if (!is_uf2_block(bl) || !UF2_IS_MY_FAMILY(bl)) {
        return;
    }

    if ((bl->flags & UF2_FLAG_NOFLASH) || bl->payloadSize != 256 || (bl->targetAddr & 0xff) ||
        bl->targetAddr < APP_START_ADDRESS || bl->targetAddr >= FLASH_SIZE) {
#if USE_DBG_MSC
        if (!quiet)
            logval("invalid target addr", bl->targetAddr);
#endif
        // this happens when we're trying to re-flash CURRENT.UF2 file previously
        // copied from a device; we still want to count these blocks to reset properly
    } else {
        valid_target = true;
        // logval("write block at", bl->targetAddr);
        flash_write_row((void *)bl->targetAddr, (void *)bl->data);
    }

    if (state && bl->numBlocks) {
        if (state->numBlocks != bl->numBlocks) {
            if (bl->numBlocks >= MAX_BLOCKS || state->numBlocks)
                state->numBlocks = 0xffffffff;
            else
                state->numBlocks = bl->numBlocks;
        }
        if (bl->blockNo < MAX_BLOCKS) {
            uint8_t mask = 1 << (bl->blockNo % 8);
            uint32_t pos = bl->blockNo / 8;
            if (!(state->writtenMask[pos] & mask)) {
                // logval("incr", state->numWritten);
                state->writtenMask[pos] |= mask;
                state->numWritten++;
            }
            if (state->numWritten >= state->numBlocks) {
                if (valid_target && bl->targetAddr >= OTA_SLOT0_START && bl->targetAddr < OTA_SLOT1_START) {
                    // A manually copied UF2 into the runtime bank must win over any
                    // older pending slot-B metadata, otherwise the next boot can copy
                    // the staged image back over the freshly written updater/app.
                    ota_force_slot0_active();
                }
                // wait a little bit before resetting, to avoid Windows transmit error
                // https://github.com/Microsoft/uf2-samd21/issues/11
                if (!quiet)
                    resetHorizon = timerHigh + 30;
                // resetIntoApp();
            }
        }
    } else {
        if (!quiet)
            resetHorizon = timerHigh + 300;
    }
}
