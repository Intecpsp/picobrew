/* Pico C SSD1325 0x200069e0; SSD1309 0x200069c8.
 * The exact 74-byte source spans match and are recorded in analysis/routines.json.
 */
#include "pico_c/recovered.h"

_Static_assert(offsetof(PicoCIPv4Address, octets) == 4, "OEM IPv4 object layout");

uint32_t pico_c_parse_ipv4_address(const char *text, PicoCIPv4Address *address)
{
    uint8_t field = 0;
    uint16_t value = 0;
    for (;;) {
        uint8_t character = (uint8_t)*text++;
        if (character == 0) {
            if (field != 3)
                return 0;
            address->octets[3] = (uint8_t)value;
            return 1;
        }
        uint8_t digit = (uint8_t)(character - '0');
        if (digit <= 9) {
            value = (uint16_t)(10u * value + digit);
            if (value > 255)
                return 0;
        } else if (character == '.' && field != 3) {
            address->octets[field++] = (uint8_t)value;
            value = 0;
        } else {
            return 0;
        }
    }
}
