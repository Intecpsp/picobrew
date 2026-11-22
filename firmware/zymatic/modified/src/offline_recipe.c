/* Zymatic 1.1.14 integration. All stock function addresses are AVR *word*
 * pointers. Only the installers for this exact application may use them. */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SRAM8(address) (*(volatile uint8_t *)(uintptr_t)(address))
#define OEM(address, result, args) ((result (*) args)(uintptr_t)((address) / 2))
#define FLASH __attribute__((section(".progmem.data"), used))

/* Patched by the private builder after validating one Zymatic-native
 * program. Byte 0 is the number of steps, bytes 1–2 are the little-endian
 * record length, and byte 3 is the name length. The remaining 512 bytes
 * contain the original ASCII record, NUL and zero padding. Application CRC
 * covers this entire field before original hardware setup starts. */
const uint8_t provision_recipe[516] __attribute__((section(".provision"), used)) = {0};
static const char network_choices[][21] FLASH = {"Set up network", "Continue offline"};
static const char network_title[] FLASH = "Network unavailable";

enum {
    RECIPE_BANK = 0x16be, STEP_NAME_CURSOR = 0x1639, RECIPE_IDS = 0x1d76,
    RECIPE_COUNT = 0x1ec5, MENU_ROWS = 0x2010, SESSION_KEY = 0x23f6,
    SESSION_KEY_EEPROM = 122, SESSION_ACTIVE_EEPROM = 1,
};


static bool local_menu;
uint8_t zymatic_offline_boot;
static const uint8_t marker = '!';

static uint8_t read_eeprom(uint16_t address)
{
    return OEM(0x4862, uint8_t, (uint16_t))(address);
}

static bool selected_local(const char *id)
{
    /* The gate protects stock online and clean sessions even if an OEM
     * resync replaced the bank without taking either fallback call site. */
    if (!local_menu || SRAM8(RECIPE_COUNT) != 1 || id != (const char *)RECIPE_IDS)
        return false;
    uint8_t rampz = RAMPZ;
    uint_farptr_t header = pgm_get_far_address(provision_recipe);
    uint8_t name_length = pgm_read_byte_far(header + 3);
    uint_farptr_t expected = header + 4 + name_length + 1;
    bool matches = true;
    for (uint8_t i = 0; i < 32; ++i)
        if (id[i] != (char)pgm_read_byte_far(expected + i))
            matches = false;
    RAMPZ = rampz;
    return matches && id[32] == 0;
}

/* Keep the record buffer off the stack during the stock network fetch. */
static __attribute__((noinline)) void load_local_recipe(void)
{
    /* All names in the previous (empty) bank are now unreachable. Use the
     * exact tokenizer, number conversion, and name-pool allocation of 1.1.14. */
    char record[512];
    uint8_t rampz = RAMPZ, header[4];
    uint_farptr_t flash = pgm_get_far_address(provision_recipe);
    memcpy_PF(header, flash, sizeof(header));
    uint16_t length = header[1] | ((uint16_t)header[2] << 8);
    if (length <= header[3] + 33u || length >= sizeof(record) || header[3] == 0 || header[3] > 20 ||
        header[0] == 0 || header[0] > 25) {
        RAMPZ = rampz;
        SRAM8(RECIPE_COUNT) = 0;
        return;
    }
    memcpy_PF(record, flash + 4, length + 1);
    RAMPZ = rampz;
    if (record[length] || record[length-1] != '/' || record[header[3]] != '/' ||
        record[header[3]+33] != '/') {
        SRAM8(RECIPE_COUNT) = 0;
        return;
    }
    typedef uint16_t (*field_fn)(uint16_t, char *, const char *);
    field_fn field = (field_fn)(0x2952 / 2);
    uint16_t offset = field(0, (char *)RECIPE_BANK, record);
    offset = field(offset, (char *)RECIPE_IDS, record);
    *(volatile uint16_t *)STEP_NAME_CURSOR = 0;
    uint8_t steps = OEM(0x894e, uint8_t, (uint8_t, const char *, uint16_t, uint16_t))
        (0, record, offset, length);
    if (steps != header[0]) {
        SRAM8(RECIPE_COUNT) = 0;
        return;
    }
    SRAM8(RECIPE_BANK + 21) = steps;
    local_menu = true;
    SRAM8(RECIPE_COUNT) = 1;
}

void zymatic_sync_or_local(uint8_t mode)
{
    /* Calling from the normal user-program sync sites preserves the stock
     * cleaning loader and power-loss recovery. Allow the OEM to refresh first
     * on a configured network, including a network that came back after boot. */
    if (selected_local((const char *)RECIPE_IDS))
        SRAM8(RECIPE_COUNT) = 0;
    local_menu = false;
    if (!zymatic_offline_boot || SRAM8(0x230b) || SRAM8(0x230c))
        OEM(0xdade, void, (uint8_t))(mode);
    uint8_t count = SRAM8(RECIPE_COUNT);
    if (mode != 0 || (count != 0 && count != 12))
        return;
    load_local_recipe();
}

void zymatic_begin_brew(uint8_t active)
{
    if (active && selected_local((const char *)RECIPE_IDS)) {
        /* Stock sets the active flag before opening a new online session.
         * Mark the old session ID unusable first, even if power is lost in
         * the interval before the OEM saves the new session key. */
        OEM(0x495c, void, (uint16_t, uint8_t))(SESSION_KEY_EEPROM, marker);
    }
    OEM(0x4bc0, void, (uint8_t))(active);
}

uint8_t zymatic_start_session(const char *user, const char *id, uint8_t mode)
{
    if (!selected_local(id))
        return OEM(0xd512, uint8_t, (const char *, const char *, uint8_t))(user, id, mode);
    memset((void *)SESSION_KEY, 0, 33);
    SRAM8(SESSION_KEY) = marker;
    return 1; /* A local session. The OEM caller still persists this exact key. */
}

uint8_t zymatic_recover_recipe(const char *key)
{
    /* There is no server reset point for a local session. Return the stock
     * 'cannot recover' result and let the OEM decide the next physical action. */
    if ((uint8_t)key[0] == marker)
        return 0;
    return OEM(0xd886, uint8_t, (const char *))(key);
}

static bool commissioned_idle(void)
{
    /* Same setup revision and identity fields tested by OEM 0x1067c. Only
     * offer a local boot to a previously commissioned, non-interrupted unit.
     * A fault or an erased EEPROM must take the original setup path. */
    if (read_eeprom(155) != 1 || read_eeprom(156) != 14 ||
        read_eeprom(SESSION_ACTIVE_EEPROM) != 0 || read_eeprom(121) != 0)
        return false;
    uint8_t name = read_eeprom(67);
    uint8_t user = read_eeprom(88);
    return name >= 0x20 && name <= 0x7e && user >= 0x21 && user <= 0x7e;
}

uint8_t zymatic_wifi_or_offline(void)
{
    if (commissioned_idle()) {
        char title[sizeof(network_title)];
        uint8_t rampz = RAMPZ;
        memcpy_PF(title, pgm_get_far_address(network_title), sizeof(title));
        memcpy_PF((void *)MENU_ROWS, pgm_get_far_address(network_choices), sizeof(network_choices));
        RAMPZ = rampz;
        if (OEM(0x81f0, uint8_t, (const char *, uint8_t, uint8_t))(title, 2, 0) == 1) {
            zymatic_offline_boot = 1;
            return 0; /* Preserve the actual disconnected Wi-Fi state. */
        }
    }
    zymatic_offline_boot = 0;
    return OEM(0xb232, uint8_t, (void))();
}
