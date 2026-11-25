// Stand-in for inc/uf2.h in the host test of src/ota_swap.c: only the layout
// and the flag record, without the chip headers. Keep the values in step with
// inc/uf2.h.
#ifndef UF2_H_HOST
#define UF2_H_HOST

#include <stdint.h>
#include <stdbool.h>

#define OTA_SLOT0_START 0x00004000UL
#define OTA_SLOT1_START 0x00021D00UL
#define OTA_FLASH_END 0x0003FB00UL
#define OTA_SLOT0_SIZE (OTA_SLOT1_START - OTA_SLOT0_START)
#define OTA_SLOT1_SIZE (OTA_FLASH_END - OTA_SLOT1_START)
#define OTA_FLAG_ADDRESS 0x0003FF00UL
#define OTA_AB_MAGIC 0x42544455UL
#define OTA_AB_VERSION 1UL
#define OTA_AB_NUM_SLOTS 2UL

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t checksum;
    uint32_t staged_size;
} OTA_AB_Flags;

static inline uint32_t ota_ab_checksum(const OTA_AB_Flags *cfg) {
    return OTA_AB_MAGIC ^ OTA_AB_VERSION ^ cfg->active_slot ^ cfg->pending_slot ^ 0xA5A55A5AUL;
}

static inline bool ota_ab_valid(const OTA_AB_Flags *cfg) {
    return cfg->magic == OTA_AB_MAGIC && cfg->version == OTA_AB_VERSION &&
           cfg->active_slot < OTA_AB_NUM_SLOTS && cfg->pending_slot < OTA_AB_NUM_SLOTS &&
           cfg->checksum == ota_ab_checksum(cfg);
}

#endif
