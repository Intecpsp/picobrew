/* Reconstructed from preserved Z-series 0.0.119 Xtensa instruction bodies.
 * Generated decompiler output is evidence only. In particular, its CRC byte
 * function lost the hardware LOOP instruction: all eight iterations below
 * are verified from original instructions at 0x400d200e..0x400d2028. */
#include "zseries/msp_protocol.h"
#include <string.h>

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* 0x400d2000: polynomial 0x1021; MSB first, no final XOR. */
uint16_t z_msp_crc_byte(uint16_t crc, uint8_t byte)
{
    uint16_t input = (uint16_t)((uint16_t)byte << 8);
    for (unsigned bit = 0; bit < 8; ++bit) {
        bool different = ((crc ^ input) & 0x8000u) != 0;
        crc = (uint16_t)(crc << 1);
        if (different) crc ^= 0x1021u;
        input = (uint16_t)(input << 1);
    }
    return crc;
}

/* 0x400d2030 initializes 0xffff and covers command/address/data, excluding
 * the framing bytes and the CRC itself. */
uint16_t z_msp_crc(const uint8_t *bytes, size_t count)
{
    uint16_t crc = 0xffffu;
    for (size_t i = 0; i < count; ++i) crc = z_msp_crc_byte(crc, bytes[i]);
    return crc;
}

/* 0x400d6164: segment order matters; first matching segment wins. The two bytes
 * at 0xc000 are excluded; gaps through 0xfbff are treated as erased (0xff). */
uint16_t z_msp_image_crc(const struct z_msp_image *image)
{
    uint16_t crc = 0xffffu;
    for (uint32_t address = 0xc002; address != 0xfc00; ++address) {
        uint8_t byte = 0xff;
        for (uint32_t i = 0; i < image->segment_count; ++i) {
            const struct z_msp_segment *segment = &image->segments[i];
            if (segment->address <= address && address < segment->address + segment->size) {
                byte = segment->bytes[address - segment->address];
                break;
            }
        }
        crc = z_msp_crc_byte(crc, byte);
    }
    return crc;
}

/* 0x400dc050, 0x400dc2f8, 0x400dc528. 0xdead is a constant trailer here,
 * not the bootloader CRC. The length byte includes the command. */
size_t z_msp_app_frame(uint8_t *out, size_t capacity, uint8_t command,
                      uint32_t value, unsigned value_bytes)
{
    if (value_bytes != 0 && value_bytes != 1 && value_bytes != 4) return 0;
    size_t count = value_bytes + 5u;
    if (out == NULL || capacity < count) return 0;
    out[0] = 0x08;
    out[1] = (uint8_t)(value_bytes + 1);
    out[2] = command;
    for (unsigned i = 0; i < value_bytes; ++i) out[3 + i] = (uint8_t)(value >> (8 * i));
    out[3 + value_bytes] = 0xad;
    out[4 + value_bytes] = 0xde;
    return count;
}

static size_t finish_bsl(uint8_t *out, size_t body_size)
{
    uint16_t crc = z_msp_crc(out + 2, body_size);
    out[0] = 0x80;
    out[1] = (uint8_t)body_size;
    out[2 + body_size] = (uint8_t)crc;
    out[3 + body_size] = (uint8_t)(crc >> 8);
    return body_size + 4;
}

/* 0x400dc5e8 and 0x400d1f60; observed framing has a one-byte length. */
size_t z_msp_bsl_simple_frame(uint8_t *out, size_t capacity, uint8_t command)
{
    if (out == NULL || capacity < 5) return 0;
    out[2] = command;
    return finish_bsl(out, 1);
}

static void bsl_address(uint8_t *out, uint8_t command, uint32_t address)
{
    out[2] = command;
    out[3] = (uint8_t)address;
    out[4] = (uint8_t)(address >> 8);
    out[5] = (uint8_t)(address >> 16);
}

/* 0x400dc630 constructs 1..16 byte chunks. High address bits are discarded
 * on the wire, matching the original 24-bit field. */
size_t z_msp_bsl_program_frame(uint8_t *out, size_t capacity, uint32_t address,
                              const uint8_t *bytes, size_t count)
{
    if (out == NULL || bytes == NULL || count == 0 || count > 16 || capacity < count + 8) return 0;
    bsl_address(out, Z_MSP_BSL_PROGRAM, address);
    memcpy(out + 6, bytes, count);
    return finish_bsl(out, count + 4);
}

size_t z_msp_bsl_erase_frame(uint8_t *out, size_t capacity, uint32_t address)
{
    if (out == NULL || capacity < 8) return 0;
    bsl_address(out, Z_MSP_BSL_ERASE_SEGMENT, address);
    return finish_bsl(out, 4);
}

/* 0x400dbd94: response type and eight payload bytes. No response checksum is
 * inspected by this ESP32 code. Unknown/type-zero/boot responses retain state. */
void z_msp_apply_response(struct z_msp_state *state, const uint8_t response[9])
{
    if (response[0] == 1) {
        state->version_major = response[1];
        state->version_minor = response[2];
    } else if (response[0] == 2) {
        state->use_flags = response[1] & 0x07;
        state->pumps = response[1] & 0xf0;
        state->fan = response[1] & 0x08;
        state->valve_position = response[2];
        state->status_byte_2 = response[3];
        state->status_byte_3 = response[4];
        state->errors = read_le32(response + 5);
        state->moving_valve = 0;
        state->last_flags = response[1];
    }
}

/* 0x400dbcec: original assertions are Msp.h lines 0x70 and 0x75. */
int z_msp_command_allowed(const struct z_msp_state *state, uint8_t command)
{
    if (state->id != Z_MSP_APP_RUNNING) return Z_MSP_WRONG_STATE;
    if (command != Z_MSP_STATE_GET && state->moving_valve) return Z_MSP_VALVE_BUSY;
    return Z_MSP_OK;
}

/* 0x400dcdc0 requires five consecutive Q bytes, not merely response type Q. */
bool z_msp_is_boot_response(const uint8_t response[9])
{
    for (unsigned i = 0; i < 5; ++i) if (response[i] != 0x51) return false;
    return true;
}

bool z_msp_is_expected_version(const uint8_t response[9])
{
    return response[0] == 1 && response[1] == 1 && response[2] == 0;
}

int z_msp_init(struct z_msp_client *client, struct z_msp_state *state,
               struct z_msp_transport transport, uint8_t address, uint32_t poll_interval,
               uint32_t transition_delay, uint32_t response_delay)
{
    if (client == NULL || state == NULL || transport.transmit == NULL ||
        transport.receive == NULL || transport.delay_ms == NULL) return Z_MSP_INVALID_ARGUMENT;
    memset(client, 0, sizeof(*client));
    client->state = state;
    client->transport = transport;
    client->address = address;
    client->poll_interval = poll_interval;
    client->transition_delay = transition_delay;
    client->response_delay = response_delay;
    return Z_MSP_OK;
}

static void transmit(struct z_msp_client *client, const uint8_t *bytes, size_t count)
{
    client->transport.transmit(client->transport.context, client->address, bytes, count);
}

static void delay(struct z_msp_client *client, uint32_t milliseconds)
{
    client->transport.delay_ms(client->transport.context, milliseconds);
}

static void send_unchecked(struct z_msp_client *client, uint8_t command,
                           uint32_t value, unsigned value_bytes)
{
    uint8_t frame[9];
    size_t count = z_msp_app_frame(frame, sizeof(frame), command, value, value_bytes);
    transmit(client, frame, count);
    delay(client, client->response_delay);
    client->transport.receive(client->transport.context, client->address, client->response, 9);
    z_msp_apply_response(client->state, client->response);
}

int z_msp_send(struct z_msp_client *client, uint8_t command, uint32_t value, unsigned value_bytes)
{
    int result = z_msp_command_allowed(client->state, command);
    if (result != Z_MSP_OK) return result;
    if (value_bytes != 0 && value_bytes != 1 && value_bytes != 4) return Z_MSP_INVALID_ARGUMENT;
    send_unchecked(client, command, value, value_bytes);
    return Z_MSP_OK;
}

/* 0x400dc1c4 uses a strict unsigned deadline comparison. Rollover behavior is
 * retained here, rather than silently repairing it in the OEM reconstruction. */
int z_msp_poll(struct z_msp_client *client, uint32_t now)
{
    if (client->state->id == Z_MSP_APP_RUNNING && client->next_poll < now) {
        client->next_poll = now + client->poll_interval;
        return z_msp_send(client, Z_MSP_STATE_GET, 0, 0);
    }
    return Z_MSP_OK;
}

/* 0x400dc984: mark movement only after the command's response is processed. */
int z_msp_move_valve(struct z_msp_client *client, uint8_t position)
{
    if (position < 1 || position > 9) return Z_MSP_INVALID_POSITION;
    int result = z_msp_send(client, (uint8_t)(Z_MSP_VALVE_BASE + position), 0, 0);
    if (result == Z_MSP_OK) client->state->moving_valve = 1;
    return result;
}

/* Original wrappers 0x400dc268 / 0x400dc2e0. */
int z_msp_step_valve(struct z_msp_client *client, bool increment)
{
    int result = z_msp_send(client, increment ? Z_MSP_STEP_INCREMENT : Z_MSP_STEP_DECREMENT, 0, 0);
    if (result == Z_MSP_OK) client->state->moving_valve = 1;
    return result;
}

/* 0x400d6108 delays before writing the new state ID. */
static void transition(struct z_msp_client *client, uint32_t id)
{
    delay(client, client->transition_delay);
    client->state->id = id;
}

int z_msp_switch_to_boot(struct z_msp_client *client)
{
    int result = z_msp_send(client, Z_MSP_ENTER_BOOT, 0, 0);
    if (result == Z_MSP_OK) transition(client, Z_MSP_BOOT_RUNNING);
    return result;
}

static int bsl_response(struct z_msp_client *client)
{
    client->transport.receive(client->transport.context, client->address, &client->last_bsl_response, 1);
    return client->last_bsl_response;
}

/* 0x400dc5e8: START_APP deliberately has no read/acknowledgement. */
int z_msp_bsl_command(struct z_msp_client *client, uint8_t command)
{
    uint8_t frame[5];
    transmit(client, frame, z_msp_bsl_simple_frame(frame, sizeof(frame), command));
    return command == Z_MSP_BSL_START_APP ? Z_MSP_OK : bsl_response(client);
}

int z_msp_start_app(struct z_msp_client *client)
{
    if (client->state->id != Z_MSP_BOOT_RUNNING) return Z_MSP_WRONG_STATE;
    (void)z_msp_bsl_command(client, Z_MSP_BSL_START_APP);
    transition(client, Z_MSP_APP_RUNNING);
    return Z_MSP_OK;
}

int z_msp_get_version(struct z_msp_client *client)
{
    /* The OEM warns, but does not assert, when response type is not 1. */
    return z_msp_send(client, Z_MSP_VERSION_GET, 0, 0);
}

/* 0x400dc630: stop at the first nonzero controller result; no retry. */
int z_msp_program_segment(struct z_msp_client *client, uint32_t address,
                          const uint8_t *bytes, uint32_t count)
{
    while (count != 0) {
        uint8_t frame[24];
        uint32_t chunk = count > 16 ? 16 : count;
        size_t frame_size = z_msp_bsl_program_frame(frame, sizeof(frame), address, bytes, chunk);
        transmit(client, frame, frame_size);
        int result = bsl_response(client);
        if (result != 0) return result;
        address += chunk;
        bytes += chunk;
        count -= chunk;
    }
    return Z_MSP_OK;
}

/* 0x400dcaa8: 30 descending 512-byte segments, from 0xfbff to 0xc1ff.
 * Nonzero result corresponds to the original fatal assertion at line 0xff. */
int z_msp_erase_app(struct z_msp_client *client)
{
    for (uint32_t address = 0xfbff; address > 0xc000; address = (address - 0x200) & 0xffff) {
        uint8_t frame[8];
        transmit(client, frame, z_msp_bsl_erase_frame(frame, sizeof(frame), address));
        int result = bsl_response(client);
        if (result != 0) return Z_MSP_ERASE_FAILED;
    }
    return Z_MSP_OK;
}

/* 0x400dcb84: controller boot version high nibble must be 0xa. */
int z_msp_update(struct z_msp_client *client, const struct z_msp_image *image)
{
    if (client->state->id != Z_MSP_BOOT_RUNNING) return Z_MSP_WRONG_STATE;
    int result = z_msp_bsl_command(client, Z_MSP_BSL_VERSION);
    if ((result & 0xf0) != 0xa0) return Z_MSP_BAD_BOOT_VERSION;
    result = z_msp_erase_app(client);
    if (result != Z_MSP_OK) return result;
    for (uint32_t i = 0; i < image->segment_count; ++i) {
        const struct z_msp_segment *segment = &image->segments[i];
        result = z_msp_program_segment(client, segment->address, segment->bytes, segment->size);
        if (result != Z_MSP_OK) return result;
    }
    return Z_MSP_OK;
}

/* 0x400dcdc0: this OEM routine expects the embedded MSP app 1.0 with CRC 0x5fdb.
 * It can program that auxiliary controller; it is not ESP32 OTA functionality. */
int z_msp_setup(struct z_msp_client *client, const struct z_msp_image *image)
{
    if (z_msp_image_crc(image) != 0x5fdb) return Z_MSP_BAD_IMAGE_CRC;
    send_unchecked(client, Z_MSP_VERSION_GET, 0, 0);
    bool boot = z_msp_is_boot_response(client->response);
    bool app = !boot && client->response[0] == 1;
    if (!boot && !app) {
        return client->state->id == Z_MSP_UNKNOWN ?
            Z_MSP_UNRECOGNIZED_INITIAL_RESPONSE : Z_MSP_WRONG_STATE;
    }
    transition(client, boot ? Z_MSP_BOOT_RUNNING : Z_MSP_APP_RUNNING);
    int result;
    if (app && (result = z_msp_switch_to_boot(client)) != Z_MSP_OK) return result;
    result = z_msp_bsl_command(client, Z_MSP_BSL_VERSION);
    if ((result & 0xf0) != 0xa0) {
        transition(client, Z_MSP_BOOTLOADER_ERROR);
        return Z_MSP_BAD_BOOT_VERSION;
    }
    (void)z_msp_start_app(client);
    result = z_msp_get_version(client);
    if (result != Z_MSP_OK) return result;
    if (!z_msp_is_expected_version(client->response)) {
        result = z_msp_switch_to_boot(client);
        if (result != Z_MSP_OK) return result;
        result = z_msp_update(client, image);
        if (result == Z_MSP_ERASE_FAILED || result == Z_MSP_WRONG_STATE) return result;
        /* The OEM logs a programming/probe failure and still attempts start +
         * version. This behavior is retained; a later modified port may change it. */
        (void)z_msp_start_app(client);
        result = z_msp_get_version(client);
        if (result != Z_MSP_OK) return result;
    }
    return client->state->version_major == 1 && client->state->version_minor == 0 ?
        Z_MSP_OK : Z_MSP_BAD_APP_VERSION;
}
