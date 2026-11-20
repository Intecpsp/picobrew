#ifndef ZSERIES_MSP_PROTOCOL_H
#define ZSERIES_MSP_PROTOCOL_H

/* Typed reconstruction of Z-series 0.0.119 Msp.h behavior. This is not vendor
 * source, a full ESP32 firmware image, or a hardware-tested I2C adapter.
 * Original addresses and validation boundaries: analysis/msp-protocol.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum z_msp_state_id {
    Z_MSP_UNKNOWN = 0,
    Z_MSP_BOOT_RUNNING = 1,
    Z_MSP_APP_RUNNING = 2,
    Z_MSP_BOOTLOADER_ERROR = 3
};

enum z_msp_command {
    Z_MSP_PUMPS_SET = 0x0d,
    Z_MSP_PUMPS_GET = 0x0e,
    Z_MSP_KEG_ON = 0x0f,
    Z_MSP_DRAIN_ON = 0x10,
    Z_MSP_HEX_ON = 0x11,
    Z_MSP_KEG_OFF = 0x12,
    Z_MSP_DRAIN_OFF = 0x13,
    Z_MSP_HEX_OFF = 0x14,
    Z_MSP_VALVE_BASE = 0x14, /* positions 1..9 add to this value */
    Z_MSP_STEP_INCREMENT = 0x1f,
    Z_MSP_STEP_DECREMENT = 0x20,
    Z_MSP_FAN_ON = 0x34,
    Z_MSP_FAN_OFF = 0x35,
    Z_MSP_SERVO_1_PULSE = 0x3c, /* through 0x43 for servos 2..8; 25-us ticks */
    Z_MSP_SERVOS_RELEASE = 0x44,
    Z_MSP_SERVOS_OPEN = 0x45,
    Z_MSP_SERVOS_CLOSE = 0x46,
    Z_MSP_SERVOS_STATE = 0x47,
    Z_MSP_SERVO_TIMER_START = 0x64,
    Z_MSP_SERVO_TIMER_STOP = 0x65,
    Z_MSP_TEST_SET_ERRORS = 0xc8,
    Z_MSP_CLEAR_ERRORS = 0xfc,
    Z_MSP_STATE_GET = 0xfd,
    Z_MSP_VERSION_GET = 0xfe,
    Z_MSP_ENTER_BOOT = 0xff
};

enum z_msp_bsl_command {
    Z_MSP_BSL_PROGRAM = 0x10,
    Z_MSP_BSL_ERASE_SEGMENT = 0x12,
    Z_MSP_BSL_VERSION = 0x19,
    Z_MSP_BSL_START_APP = 0x1c
};

enum z_msp_result {
    Z_MSP_OK = 0,
    Z_MSP_INVALID_ARGUMENT = -1,
    Z_MSP_WRONG_STATE = -2,
    Z_MSP_VALVE_BUSY = -3,
    Z_MSP_INVALID_POSITION = -4,
    Z_MSP_BAD_BOOT_VERSION = -5,
    Z_MSP_BAD_IMAGE_CRC = -6,
    Z_MSP_BAD_APP_VERSION = -7,
    Z_MSP_UNRECOGNIZED_INITIAL_RESPONSE = -8,
    Z_MSP_ERASE_FAILED = -9
};

/* Portable data model: no original C++ vtable or ESP32 ABI padding is exposed.
 * Both unknown status bytes are deliberately left without guessed names. */
struct z_msp_state {
    uint32_t id;
    uint8_t use_flags;       /* original state +0x08: response flags & 0x07 */
    uint8_t pumps;           /* +0x09: flags & 0xf0, not shifted */
    uint8_t fan;             /* +0x0a: flags & 0x08, not converted to bool */
    uint8_t valve_position;  /* +0x0b: payload byte 1 */
    uint8_t status_byte_2;   /* +0x0c: payload byte 2; physical meaning unresolved */
    uint8_t status_byte_3;   /* +0x0d: payload byte 3; physical meaning unresolved */
    uint32_t errors;         /* +0x10: payload bytes 4..7, little endian */
    uint8_t moving_valve;    /* +0x14 */
    uint8_t version_major;   /* +0x15 */
    uint8_t version_minor;   /* +0x16 */
    uint8_t last_flags;      /* +0x17: complete status flag byte */
};

struct z_msp_segment {
    uint32_t address;
    uint32_t size;
    const uint8_t *bytes;
};

struct z_msp_image {
    uint32_t segment_count;
    const struct z_msp_segment *segments;
};

/* Transport is an explicit unresolved platform boundary. transmit must perform
 * beginTransmission(address), byte writes, endTransmission(true); receive must
 * perform requestFrom(address,count,true), yield until available >= count, and
 * read exactly count bytes. The OEM ignores endTransmission's result and has
 * no receive deadline. No pins, bus frequency, or physical I/O are supplied here.
 * These callbacks must complete synchronously or not return; a platform adapter
 * may later add an explicitly documented failure/cancellation policy. */
struct z_msp_transport {
    void *context;
    void (*transmit)(void *, uint8_t address, const uint8_t *, size_t);
    void (*receive)(void *, uint8_t address, uint8_t *, size_t);
    void (*delay_ms)(void *, uint32_t);
};

struct z_msp_client {
    struct z_msp_state *state;
    struct z_msp_transport transport;
    uint8_t address;
    uint8_t response[9];
    uint32_t next_poll;
    uint32_t poll_interval;
    uint32_t transition_delay;
    uint32_t response_delay;
    uint8_t last_bsl_response;
};

/* Pure byte transformations, independently callable without any transport. */
uint16_t z_msp_crc_byte(uint16_t crc, uint8_t byte);
uint16_t z_msp_crc(const uint8_t *bytes, size_t count);
uint16_t z_msp_image_crc(const struct z_msp_image *image);
size_t z_msp_app_frame(uint8_t *out, size_t capacity, uint8_t command,
                      uint32_t value, unsigned value_bytes);
size_t z_msp_bsl_simple_frame(uint8_t *out, size_t capacity, uint8_t command);
size_t z_msp_bsl_program_frame(uint8_t *out, size_t capacity, uint32_t address,
                              const uint8_t *bytes, size_t count);
size_t z_msp_bsl_erase_frame(uint8_t *out, size_t capacity, uint32_t address);
void z_msp_apply_response(struct z_msp_state *, const uint8_t response[9]);
int z_msp_command_allowed(const struct z_msp_state *, uint8_t command);
bool z_msp_is_boot_response(const uint8_t response[9]);
bool z_msp_is_expected_version(const uint8_t response[9]);

/* Parameters are supplied explicitly: test fixture addresses/delays are not OEM
 * defaults. init initializes client bookkeeping but preserves the supplied state.
 * Negative results replace OEM fatal assertions at the documented boundaries;
 * positive programming results are unchanged one-byte controller error codes. */
int z_msp_init(struct z_msp_client *, struct z_msp_state *,
               struct z_msp_transport, uint8_t address, uint32_t poll_interval,
               uint32_t transition_delay, uint32_t response_delay);
int z_msp_send(struct z_msp_client *, uint8_t command, uint32_t value, unsigned value_bytes);
int z_msp_poll(struct z_msp_client *, uint32_t now);
int z_msp_move_valve(struct z_msp_client *, uint8_t position);
int z_msp_step_valve(struct z_msp_client *, bool increment);
int z_msp_switch_to_boot(struct z_msp_client *);
int z_msp_start_app(struct z_msp_client *);
int z_msp_get_version(struct z_msp_client *);
int z_msp_bsl_command(struct z_msp_client *, uint8_t command);
int z_msp_program_segment(struct z_msp_client *, uint32_t address,
                          const uint8_t *bytes, uint32_t count);
int z_msp_erase_app(struct z_msp_client *);
int z_msp_update(struct z_msp_client *, const struct z_msp_image *);
int z_msp_setup(struct z_msp_client *, const struct z_msp_image *);

#ifdef __cplusplus
}
#endif
#endif
