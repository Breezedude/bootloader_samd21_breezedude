#include "uf2.h"

void led_tick(void) {
}

static bool app_valid(uint32_t app_base) {
    uint32_t stack_ptr = *(uint32_t *)app_base;
    uint32_t reset_vec = *(uint32_t *)(app_base + 4);

#if defined(SAMD21)
    bool stack_ok = (stack_ptr >= HMCRAMC0_ADDR) &&
                    (stack_ptr <= (HMCRAMC0_ADDR + HMCRAMC0_SIZE)) &&
                    ((stack_ptr & 0x3u) == 0u);
#elif defined(SAMD51)
    bool stack_ok = (stack_ptr >= HSRAM_ADDR) &&
                    (stack_ptr <= (HSRAM_ADDR + HSRAM_SIZE)) &&
                    ((stack_ptr & 0x3u) == 0u);
#else
    bool stack_ok = ((stack_ptr & 0x3u) == 0u);
#endif

    return stack_ok && (reset_vec & 1u) && reset_vec >= OTA_SLOT0_START && reset_vec < FLASH_SIZE;
}

int main(void) {
    uint32_t app_base = OTA_SLOT0_START;
    uint32_t app_start_address;

    __disable_irq();
    __DMB();

    if (!app_valid(app_base)) {
        while (1) {
        }
    }

    app_start_address = *(uint32_t *)(app_base + 4);

    __set_MSP(*(uint32_t *)app_base);
    SCB->VTOR = (app_base & SCB_VTOR_TBLOFF_Msk);
    asm("bx %0" : : "r"(app_start_address));

    while (1) {
    }
}
