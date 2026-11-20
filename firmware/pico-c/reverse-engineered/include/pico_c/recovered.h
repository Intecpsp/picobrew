#ifndef PICO_C_RECOVERED_H
#define PICO_C_RECOVERED_H

#include <stddef.h>
#include <stdint.h>

/* Layout recovered from accesses in both original Pico C variants. */
typedef struct PicoCEncoder {
    uint8_t phase_a_pin, phase_b_pin, button_pin;
    uint8_t reserved_03[9];
    volatile uint8_t click_latch;
    uint8_t reserved_0d[3];
    int32_t minimum, maximum, previous_position, step;
    volatile int32_t position;
    uint8_t button_interrupt, phase_a_interrupt, phase_b_interrupt, reserved_27;
    uint8_t wrap_enabled, previous_phase, current_phase, last_stable_phase;
} PicoCEncoder;

typedef enum PicoCEncoderInterrupt {
    PICO_C_ENCODER_ROTATION_INTERRUPT,
    PICO_C_ENCODER_BUTTON_INTERRUPT
} PicoCEncoderInterrupt;

/* Explicit call boundary: no machine pins or peripheral emulation are invented.
 * Handler identities map to recovered C variant entries in analysis/routines.json.
 */
typedef struct PicoCEncoderOps {
    uint32_t (*digital_read)(void *context, uint32_t pin);
    void (*pin_mode)(void *context, uint32_t pin, uint32_t mode);
    void (*interrupts_disable)(void *context);
    void (*interrupts_enable)(void *context);
    void (*attach_interrupt)(void *context, uint32_t interrupt,
                             PicoCEncoderInterrupt handler, uint32_t mode);
} PicoCEncoderOps;

void pico_c_encoder_construct(PicoCEncoder *encoder);
void pico_c_encoder_set_step(PicoCEncoder *encoder, int32_t step);
void pico_c_encoder_set_range(PicoCEncoder *encoder, int32_t minimum,
                              int32_t maximum, int32_t position);
void pico_c_encoder_enable_wrap(PicoCEncoder *encoder);
void pico_c_encoder_disable_wrap(PicoCEncoder *encoder);
void pico_c_encoder_set_position(PicoCEncoder *encoder, int32_t position);
int32_t pico_c_encoder_get_position(PicoCEncoder *encoder);
uint32_t pico_c_encoder_take_click(PicoCEncoder *encoder);
void pico_c_encoder_clear_click(PicoCEncoder *encoder);
void pico_c_encoder_attach(PicoCEncoder *encoder, const PicoCEncoderOps *ops,
                            void *context);
void pico_c_encoder_initialize(PicoCEncoder *encoder, uint8_t phase_a_pin,
                               uint8_t phase_b_pin, uint8_t button_pin,
                               uint8_t button_interrupt, uint8_t phase_a_interrupt,
                               uint8_t phase_b_interrupt, const PicoCEncoderOps *ops,
                               void *context);
void pico_c_encoder_handle_rotation(PicoCEncoder *encoder, const PicoCEncoderOps *ops,
                                    void *context);

typedef struct PicoCIPv4Address {
    uint32_t opaque_prefix;
    uint8_t octets[4];
} PicoCIPv4Address;

/* OEM parser: empty octets are accepted as zero, and a failed parse may have
 * already changed earlier octets. This is reconstruction, not new API validation.
 */
uint32_t pico_c_parse_ipv4_address(const char *text, PicoCIPv4Address *address);

#endif
