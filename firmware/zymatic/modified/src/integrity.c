/* Two independent checks with different scopes: the OEM board's persistent
 * checksum for the base release, and a CRC over the entire composed program. */
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <stdint.h>

const uint32_t provision_crc __attribute__((section(".image_crc"), used)) = 0;
const uint16_t provision_legacy_sum __attribute__((section(".provision"), used)) = 0;

uint8_t zymatic_image_valid(void)
{
    uint8_t saved = RAMPZ;
    uint32_t crc = 0xffffffffUL;
    uint_farptr_t stop = pgm_get_far_address(provision_crc);
    for (uint_farptr_t p = 0; p < stop; ++p) {
        crc ^= pgm_read_byte_far(p);
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320UL : 0);
        if ((uint8_t)p == 255) wdt_reset();
    }
    uint32_t expected = pgm_read_dword_far(stop);
    RAMPZ = saved;
    return (crc ^ 0xffffffffUL) == expected;
}

/* EEPROM retains the original 1.1.14 release identity across application
 * replacement. Both our diagnostic and our commissioning path require the
 * original calculation over the *running* image to yield its verified sum. */
#define OEM_RELEASE_SUM 0x4d16u
static uint16_t expected_live_sum(void)
{
    uint8_t saved = RAMPZ;
    uint16_t result = pgm_read_word_far(pgm_get_far_address(provision_legacy_sum));
    RAMPZ = saved;
    return result;
}

uint8_t zymatic_factory_matches(uint16_t eeprom_stamp, uint16_t live_sum)
{
    return live_sum == expected_live_sum() && eeprom_stamp == OEM_RELEASE_SUM;
}

__attribute__((noreturn, noinline)) void zymatic_image_invalid(void)
{
    cli();
    /* Stop before entering OEM machine setup. Power cycling re-enters the
     * existing USB bootloader and allows restoration of a verified image. */
    for (;;) { }
}

uint16_t zymatic_commissioned_sum(uint16_t live_sum)
{
    if (live_sum != expected_live_sum()) zymatic_image_invalid();
    return OEM_RELEASE_SUM;
}
