/* Reconstructed from preserved Z-series 0.0.119 Xtensa instruction bodies.
 * Generated decompiler output is evidence only. In particular, its CRC byte
 * function lost the hardware LOOP instruction: all eight iterations below
 * are verified from original instructions at 0x400d200e..0x400d2028. */
#include "zseries/msp_protocol.h"
#include <stdio.h>
#include <string.h>

static int diagnostic_assert(struct z_msp_client *client, uint32_t line,
                             const char *expression, int guarded_result)
{
    const struct z_msp_diagnostics *services = client->diagnostics;
    if (services == NULL) return guarded_result;
    if (services->assertion == NULL) return Z_MSP_MISSING_DIAGNOSTIC;
    return services->assertion(services->context, "Msp.h", line, expression) ?
        Z_MSP_OK : Z_MSP_DIAGNOSTIC_FAILED;
}

static int diagnostic_log(struct z_msp_client *client, unsigned level,
                          const char *tag, const char *file, uint32_t line,
                          const char *message)
{
    const struct z_msp_diagnostics *services = client->diagnostics;
    if (services == NULL) return Z_MSP_OK;
    if (services->log == NULL) return Z_MSP_MISSING_DIAGNOSTIC;
    return services->log(services->context, level, tag, file, line, message) ?
        Z_MSP_OK : Z_MSP_DIAGNOSTIC_FAILED;
}

static int trace(struct z_msp_client *client, bool begin, uint32_t line,
                 const char *name)
{
    const struct z_msp_diagnostics *services = client->diagnostics;
    if (services == NULL) return Z_MSP_OK;
    if (services->trace == NULL) return Z_MSP_MISSING_DIAGNOSTIC;
    return services->trace(services->context, begin, "BSL", "Msp.h", line, name) ?
        Z_MSP_OK : Z_MSP_DIAGNOSTIC_FAILED;
}

static int trace_end(struct z_msp_client *client, uint32_t line,
                     const char *name, int result)
{
    int closed = trace(client, false, line, name);
    return closed == Z_MSP_OK ? result : closed;
}

static int require_boot(struct z_msp_client *client)
{
    return client->state->id == Z_MSP_BOOT_RUNNING ? Z_MSP_OK :
        diagnostic_assert(client, 0x7a, "_mspState.id == MspStateId::BootRunning",
                          Z_MSP_WRONG_STATE);
}

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

static int receive_application(struct z_msp_client *client)
{
    uint8_t *response = client->response;
    struct z_msp_state *state = client->state;
    client->transport.receive(client->transport.context, client->address, response, 9);
    if (client->diagnostics == NULL) {
        z_msp_apply_response(state, response);
        return Z_MSP_OK;
    }
    int result;
    char message[128];
#define MSP_RESPONSE_LOG(level,tag,file,line,text) do { result = diagnostic_log(client,level,tag,file,line,text); if (result != Z_MSP_OK) return result; } while (false)
    if (response[0] == 0x51) {
        MSP_RESPONSE_LOG(0, "MSP", "Msp.h", 0x114, "BOOT appears to be running");
    } else if (response[0] == 1) {
        MSP_RESPONSE_LOG(0, "MSP", "Msp.h", 0x116, "APP  appears to be running");
        state->version_major = response[1];state->version_minor = response[2];
        (void)snprintf(message, sizeof(message), "APP_VERSION_MAJ = %u", response[1]);
        MSP_RESPONSE_LOG(0, "MSPA", NULL, 0, message);
        (void)snprintf(message, sizeof(message), "APP_VERSION_MIN = %u", response[2]);
        MSP_RESPONSE_LOG(0, "MSPA", NULL, 0, message);
    } else if (response[0] == 2) {
        const uint32_t errors = read_le32(response + 5);
        if (errors != state->errors) {
            (void)snprintf(message, sizeof(message), "Errors 0x%x", (unsigned)errors);
            MSP_RESPONSE_LOG(2, "MSP", "Msp.h", 0x126, message);
        }
        if (state->last_flags != response[1]) {
            (void)snprintf(message, sizeof(message), "Pumps/Fans/Use state changed 0x%x -> 0x%x", state->last_flags, response[1]);
            MSP_RESPONSE_LOG(0, "MSP", "Msp.h", 0x12b, message);
            if ((state->last_flags & 1) != (response[1] & 1)) {
                MSP_RESPONSE_LOG(0, "MSPA", NULL, 0, (response[1] & 1) ? "MSP_BARE_PCB = True" : "MSP_BARE_PCB = False");
            }
            state->last_flags = response[1];
        }
        z_msp_apply_response(state, response);
    } else if (response[0] != 0) {
        (void)snprintf(message, sizeof(message), "Ignoring response 0x%x", response[0]);
        MSP_RESPONSE_LOG(1, "MSP", "Msp.h", 0x13f, message);
        (void)snprintf(message, sizeof(message), "0x  %02x %02x %02x %02x %02x %02x %02x %02x",
            response[1], response[2], response[3], response[4], response[5], response[6], response[7], response[8]);
        MSP_RESPONSE_LOG(1, "MSP", "Msp.h", 0x148, message);
    }
#undef MSP_RESPONSE_LOG
    return Z_MSP_OK;
}

static int send_unchecked(struct z_msp_client *client, uint8_t command,
                           uint32_t value, unsigned value_bytes)
{
    uint8_t frame[9];
    size_t count = z_msp_app_frame(frame, sizeof(frame), command, value, value_bytes);
    transmit(client, frame, count);
    delay(client, client->response_delay);
    return receive_application(client);
}

int z_msp_send(struct z_msp_client *client, uint8_t command, uint32_t value, unsigned value_bytes)
{
    int result;
    if (client->state->id != Z_MSP_APP_RUNNING) {
        result = diagnostic_assert(client, 0x70, "_mspState.id == MspStateId::AppRunning", Z_MSP_WRONG_STATE);
        if (result != Z_MSP_OK) return result;
    }
    if (command != Z_MSP_STATE_GET && client->state->moving_valve) {
        result = diagnostic_assert(client, 0x75, "cmd == COMMAND_STATE_GET || !_mspState.isMovingValve", Z_MSP_VALVE_BUSY);
        if (result != Z_MSP_OK) return result;
    }
    if (value_bytes != 0 && value_bytes != 1 && value_bytes != 4) return Z_MSP_INVALID_ARGUMENT;
    return send_unchecked(client, command, value, value_bytes);
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
    if (position < 1 || position > 9) {
        int result = diagnostic_assert(client, 0x340, "IS_VALID_VALVE_POSITION(position)", Z_MSP_INVALID_POSITION);
        if (result != Z_MSP_OK) return result;
    }
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
static const char *state_name(uint32_t id)
{
    static const char *names[] = {"Unknown", "BootRunning", "AppRunning", "BootloaderError"};
    return id < 4 ? names[id] : "<MspStateId::INVALID>";
}
static int transition(struct z_msp_client *client, uint32_t id)
{
    char message[96];
    (void)snprintf(message, sizeof(message), "TRANSITION = %s -> %s", state_name(client->state->id), state_name(id));
    int result = diagnostic_log(client, 0, "MSP", NULL, 0, message);
    if (result != Z_MSP_OK) return result;
    delay(client, client->transition_delay);
    client->state->id = id;
    return Z_MSP_OK;
}

int z_msp_switch_to_boot(struct z_msp_client *client)
{
    int result = z_msp_send(client, Z_MSP_ENTER_BOOT, 0, 0);
    if (result == Z_MSP_OK) result = transition(client, Z_MSP_BOOT_RUNNING);
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
    int result = require_boot(client);
    if (result != Z_MSP_OK) return result;
    result = z_msp_bsl_command(client, Z_MSP_BSL_START_APP);
    if (result != Z_MSP_OK) {
        result = diagnostic_assert(client, 0x357, "res == BSL_RES_OK", result);
        if (result != Z_MSP_OK) return result;
    }
    return transition(client, Z_MSP_APP_RUNNING);
}

int z_msp_get_version(struct z_msp_client *client)
{
    int result = z_msp_send(client, Z_MSP_VERSION_GET, 0, 0);
    if (result != Z_MSP_OK) return result;
    return client->response[0] == 1 ? Z_MSP_OK :
        diagnostic_log(client, 1, "MSP", "Msp.h", 0x365, "Cannot get APP version");
}

/* 0x400dc630: stop at the first nonzero controller result; no retry. */
int z_msp_program_segment(struct z_msp_client *client, uint32_t address,
                          const uint8_t *bytes, uint32_t count)
{
    int result = trace(client, true, 0xcb, "BSL_programMemorySegment");
    if (result != Z_MSP_OK) return result;
    while (count != 0) {
        uint8_t frame[24];
        uint32_t chunk = count > 16 ? 16 : count;
        size_t frame_size = z_msp_bsl_program_frame(frame, sizeof(frame), address, bytes, chunk);
        transmit(client, frame, frame_size);
        result = bsl_response(client);
        if (result != 0) break;
        address += chunk;
        bytes += chunk;
        count -= chunk;
    }
    return trace_end(client, 0xcb, "BSL_programMemorySegment", result);
}

/* 0x400dcaa8: 30 descending 512-byte segments, from 0xfbff to 0xc1ff.
 * Nonzero result invokes the returning assertion at line 0xff, then the
 * original result remains available to the caller after the scope closes. */
int z_msp_erase_app(struct z_msp_client *client)
{
    int result = trace(client, true, 0xf9, "erase");
    if (result != Z_MSP_OK) return result;
    for (uint32_t address = 0xfbff; address > 0xc000; address = (address - 0x200) & 0xffff) {
        uint8_t frame[8];
        transmit(client, frame, z_msp_bsl_erase_frame(frame, sizeof(frame), address));
        result = bsl_response(client);
        if (result != 0) break;
    }
    if (result != 0) {
        int diagnostic = diagnostic_assert(client, 0xff, "res == BSL_RES_OK", Z_MSP_ERASE_FAILED);
        if (diagnostic != Z_MSP_OK) result = diagnostic;
    }
    return trace_end(client, 0xf9, "erase", result);
}

/* 0x400dcb84: controller boot version high nibble must be 0xa. */
int z_msp_update(struct z_msp_client *client, const struct z_msp_image *image)
{
    int result = trace(client, true, 0x375, "update");
    if (result != Z_MSP_OK) return result;
    result = require_boot(client);
    if (result != Z_MSP_OK) goto finished;
    result = z_msp_bsl_command(client, Z_MSP_BSL_VERSION);
    if ((result & 0xf0) != 0xa0) {
        result = client->diagnostics == NULL ? Z_MSP_BAD_BOOT_VERSION :
            diagnostic_log(client, 2, "MSP", "Msp.h", 0x37a, "Unexpected boot loader version");
        goto finished;
    }
    result = z_msp_erase_app(client);
    if (result != Z_MSP_OK) {
        if (client->diagnostics != NULL && result > 0) {
            char message[32];(void)snprintf(message, sizeof(message), "RESPONSE 0x%x", (unsigned)result);
            result = diagnostic_log(client, 2, "BSL", "Msp.h", 0x37f, message);
        }
        goto finished;
    }
    for (uint32_t i = 0; i < image->segment_count; ++i) {
        const struct z_msp_segment *segment = &image->segments[i];
        result = z_msp_program_segment(client, segment->address, segment->bytes, segment->size);
        if (result != Z_MSP_OK) {
            if (client->diagnostics != NULL && result > 0) {
                char message[32];(void)snprintf(message, sizeof(message), "RESPONSE 0x%x", (unsigned)result);
                result = diagnostic_log(client, 2, "BSL", "Msp.h", 0x383, message);
            }
            break;
        }
    }
finished:
    return trace_end(client, 0x375, "update", result);
}

/* 0x400dcdc0: this OEM routine expects the embedded MSP app 1.0 with CRC 0x5fdb.
 * It can program that auxiliary controller; it is not ESP32 OTA functionality. */
static int setup_guarded(struct z_msp_client *client, const struct z_msp_image *image)
{
    if (z_msp_image_crc(image) != 0x5fdb) return Z_MSP_BAD_IMAGE_CRC;
    (void)send_unchecked(client, Z_MSP_VERSION_GET, 0, 0);
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

int z_msp_setup(struct z_msp_client *client, const struct z_msp_image *image)
{
    if (client->diagnostics == NULL) return setup_guarded(client, image);
    int result = trace(client, true, 0x29a, "_setupMsp");
    if (result != Z_MSP_OK) return result;
    bool boot, app, update_required;
#define MSP_SETUP_CALL(expression) do { result = (expression); if (result != Z_MSP_OK) goto finished; } while (false)
    if (z_msp_image_crc(image) != 0x5fdb) {
        MSP_SETUP_CALL(diagnostic_assert(client, 0x1eb, "crc == bcrc", Z_MSP_BAD_IMAGE_CRC));
    }
    MSP_SETUP_CALL(send_unchecked(client, Z_MSP_VERSION_GET, 0, 0));
    boot = z_msp_is_boot_response(client->response);
    app = !boot && client->response[0] == 1;
    MSP_SETUP_CALL(diagnostic_log(client, 0, "MSP", NULL, 0, boot ? "bootRunning = True" : "bootRunning = False"));
    MSP_SETUP_CALL(diagnostic_log(client, 0, "MSP", NULL, 0, app ? "appRunning = True" : "appRunning = False"));
    if (!boot && !app) {
        MSP_SETUP_CALL(diagnostic_log(client, 2, "MSP", "Msp.h", 0x2ba, "Could not figure out what MSP is currently running"));
        if (client->state->id != Z_MSP_UNKNOWN) {
            MSP_SETUP_CALL(diagnostic_assert(client, 0x2bd, "_mspState.id == MspStateId::Unknown", Z_MSP_WRONG_STATE));
        }
        goto finished;
    }
    MSP_SETUP_CALL(transition(client, boot ? Z_MSP_BOOT_RUNNING : Z_MSP_APP_RUNNING));
    if (app) MSP_SETUP_CALL(z_msp_switch_to_boot(client));
    result = z_msp_bsl_command(client, Z_MSP_BSL_VERSION);
    if ((result & 0xf0) != 0xa0) {
        MSP_SETUP_CALL(diagnostic_log(client, 2, "MSP", "Msp.h", 0x2cf, "Bootloader running on MSP has incompatible version"));
        MSP_SETUP_CALL(transition(client, Z_MSP_BOOTLOADER_ERROR));
        goto finished;
    }
    MSP_SETUP_CALL(z_msp_start_app(client));
    MSP_SETUP_CALL(z_msp_get_version(client));
    if (z_msp_is_boot_response(client->response) || client->response[0] != 1) {
        MSP_SETUP_CALL(diagnostic_log(client, 1, "MSP", "Msp.h", 0x2e1,
            "Could not start the app and get its version (MSP presumably never flashed)"));
        update_required = true;
    } else if (!z_msp_is_expected_version(client->response)) {
        MSP_SETUP_CALL(diagnostic_log(client, 0, "MSP", "Msp.h", 0x2e4, "App Version mismatch"));
        update_required = true;
    } else update_required = false;
    MSP_SETUP_CALL(diagnostic_log(client, 0, "MSP", NULL, 0, update_required ? "updateRequired = True" : "updateRequired = False"));
    if (update_required) {
        MSP_SETUP_CALL(z_msp_switch_to_boot(client));
        MSP_SETUP_CALL(z_msp_update(client, image));
        MSP_SETUP_CALL(z_msp_start_app(client));
        MSP_SETUP_CALL(z_msp_get_version(client));
    }
    if (client->state->version_major != 1) {
        MSP_SETUP_CALL(diagnostic_assert(client, 0x2f0, "_mspState.versionMaj == APP_VERSION_MAJ", Z_MSP_BAD_APP_VERSION));
    }
    if (client->state->version_minor != 0) {
        MSP_SETUP_CALL(diagnostic_assert(client, 0x2f1, "_mspState.versionMin == APP_VERSION_MIN", Z_MSP_BAD_APP_VERSION));
    }
finished:
#undef MSP_SETUP_CALL
    return trace_end(client, 0x29a, "_setupMsp", result);
}
