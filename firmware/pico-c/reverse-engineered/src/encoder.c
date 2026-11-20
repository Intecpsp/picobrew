/* Readable reconstruction from the two preserved Pico C 0.1.34 images.
 * Original entries, byte hashes and comparison coverage: analysis/routines.json.
 * The callback interface makes observed GPIO/interrupt operations testable;
 * it is not a complete appliance hardware implementation.
 */
#include "pico_c/recovered.h"
#include <string.h>

_Static_assert(sizeof(PicoCEncoder) == 44, "Pico C encoder layout");
_Static_assert(offsetof(PicoCEncoder, click_latch) == 0x0c, "click latch offset");
_Static_assert(offsetof(PicoCEncoder, minimum) == 0x10, "minimum offset");
_Static_assert(offsetof(PicoCEncoder, position) == 0x20, "position offset");
_Static_assert(offsetof(PicoCEncoder, wrap_enabled) == 0x28, "wrap offset");

void pico_c_encoder_construct(PicoCEncoder *encoder)
{
    encoder->position = 0;
    encoder->minimum = 0;
    encoder->maximum = 0;
    encoder->wrap_enabled = 0;
    encoder->step = 1;
}

void pico_c_encoder_set_step(PicoCEncoder *encoder, int32_t step)
{
    encoder->step = step;
}

void pico_c_encoder_set_range(PicoCEncoder *encoder, int32_t minimum,
                              int32_t maximum, int32_t position)
{
    encoder->position = position;
    int32_t observed_position = encoder->position;
    encoder->minimum = minimum;
    encoder->maximum = maximum;
    encoder->previous_position = observed_position;
}

void pico_c_encoder_enable_wrap(PicoCEncoder *encoder)
{
    encoder->wrap_enabled = 255;
}

void pico_c_encoder_disable_wrap(PicoCEncoder *encoder)
{
    encoder->wrap_enabled = 0;
}

void pico_c_encoder_set_position(PicoCEncoder *encoder, int32_t position)
{
    encoder->position = position;
    encoder->previous_position = encoder->position;
}

int32_t pico_c_encoder_get_position(PicoCEncoder *encoder)
{
    if (encoder->wrap_enabled) {
        if (encoder->position < encoder->minimum)
            encoder->position = encoder->maximum;
        else if (encoder->position > encoder->maximum)
            encoder->position = encoder->minimum;
    } else {
        if (encoder->position > encoder->maximum)
            encoder->position = encoder->maximum;
        if (encoder->position < encoder->minimum)
            encoder->position = encoder->minimum;
    }
    return encoder->position;
}

uint32_t pico_c_encoder_take_click(PicoCEncoder *encoder)
{
    if (encoder->click_latch != 0)
        return 0;
    encoder->click_latch = 1;
    return 255;
}

void pico_c_encoder_clear_click(PicoCEncoder *encoder)
{
    encoder->click_latch = 1;
}

void pico_c_encoder_attach(PicoCEncoder *encoder, const PicoCEncoderOps *ops,
                            void *context)
{
    ops->interrupts_disable(context);
    ops->attach_interrupt(context, encoder->phase_a_interrupt,
                          PICO_C_ENCODER_ROTATION_INTERRUPT, 4);
    ops->attach_interrupt(context, encoder->phase_b_interrupt,
                          PICO_C_ENCODER_ROTATION_INTERRUPT, 4);
    ops->attach_interrupt(context, encoder->button_interrupt,
                          PICO_C_ENCODER_BUTTON_INTERRUPT, 3);
    ops->interrupts_enable(context);
}

void pico_c_encoder_initialize(PicoCEncoder *encoder, uint8_t phase_a_pin,
                               uint8_t phase_b_pin, uint8_t button_pin,
                               uint8_t button_interrupt, uint8_t phase_a_interrupt,
                               uint8_t phase_b_interrupt, const PicoCEncoderOps *ops,
                               void *context)
{
    encoder->click_latch = 1;
    encoder->position = 0;
    /* The OEM reads the object's prior pins before storing the supplied pins. */
    uint32_t phase_a = ops->digital_read(context, encoder->phase_a_pin);
    uint32_t phase_b = ops->digital_read(context, encoder->phase_b_pin);
    uint8_t phase = (uint8_t)(phase_b + 2u * phase_a);
    encoder->button_interrupt = button_interrupt;
    encoder->previous_phase = phase;
    encoder->last_stable_phase = phase;
    encoder->phase_a_interrupt = phase_a_interrupt;
    encoder->phase_b_interrupt = phase_b_interrupt;
    encoder->phase_a_pin = phase_a_pin;
    encoder->phase_b_pin = phase_b_pin;
    encoder->button_pin = button_pin;
    ops->pin_mode(context, phase_a_pin, 0);
    ops->pin_mode(context, phase_b_pin, 0);
    ops->pin_mode(context, button_pin, 0);
    pico_c_encoder_attach(encoder, ops, context);
}

static int32_t wrapped_add(int32_t value, int delta)
{
    uint32_t bits = (uint32_t)value + (uint32_t)delta;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

void pico_c_encoder_handle_rotation(PicoCEncoder *encoder, const PicoCEncoderOps *ops,
                                    void *context)
{
    uint32_t phase_a = ops->digital_read(context, encoder->phase_a_pin);
    uint32_t phase_b = ops->digital_read(context, encoder->phase_b_pin);
    uint8_t phase = (uint8_t)(phase_b + 2u * phase_a);
    encoder->current_phase = phase;
    if (encoder->last_stable_phase != phase && (phase == 0 || phase == 3)) {
        if (encoder->previous_phase == 1)
            encoder->position = wrapped_add(encoder->position, phase == 0 ? -1 : 1);
        else if (encoder->previous_phase == 2)
            encoder->position = wrapped_add(encoder->position, phase == 0 ? 1 : -1);
        encoder->last_stable_phase = phase;
    }
    encoder->previous_phase = phase;
    encoder->position = pico_c_encoder_get_position(encoder);
}
