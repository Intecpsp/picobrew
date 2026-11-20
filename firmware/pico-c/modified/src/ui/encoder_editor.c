/* Pico C encoder attachment and two-row character editor. */
#include "ui_internal.h"

void pico_encoder_rotation_isr(void)
{
    pico_encoder_handle_rotation(&pico_ui_active->encoder);
}
void pico_encoder_button_isr(void)
{
    ui_interrupts_disable();
    ui_delay(20);
    if (!ui_read(pico_ui_active->encoder.button_pin))
        pico_ui_active->encoder.click_latch = 0;
    ui_interrupts_enable();
}
void pico_encoder_initialize(PicoEncoder *encoder, uint8_t phase_a_pin, uint8_t phase_b_pin,
                             uint8_t button_pin, uint8_t button_interrupt,
                             uint8_t phase_a_interrupt, uint8_t phase_b_interrupt)
{
    encoder->click_latch = 1;
    encoder->position = 0;
    /* The original samples the object's previous pin assignments before storing
     * the new ones. Preserve this ordering, including on the first setup. */
    bool phase_a = ui_read(encoder->phase_a_pin), phase_b = ui_read(encoder->phase_b_pin);
    uint8_t phase = (uint8_t)(phase_b + 2 * phase_a);
    encoder->button_interrupt = button_interrupt;
    encoder->previous_phase = phase;
    encoder->last_stable_phase = phase;
    encoder->phase_a_interrupt = phase_a_interrupt;
    encoder->phase_b_interrupt = phase_b_interrupt;
    encoder->phase_a_pin = phase_a_pin;
    encoder->phase_b_pin = phase_b_pin;
    encoder->button_pin = button_pin;
    ui_pin_mode(phase_a_pin, 0);
    ui_pin_mode(phase_b_pin, 0);
    ui_pin_mode(button_pin, 0);
    ui_interrupts_disable();
    ui_attach(phase_a_interrupt, pico_encoder_rotation_isr, 4);
    ui_attach(phase_b_interrupt, pico_encoder_rotation_isr, 4);
    ui_attach(button_interrupt, pico_encoder_button_isr, 3);
    ui_interrupts_enable();
}

static void editor_character(int index, int x, bool selected, const char characters[100])
{
    const uint8_t *bitmap = NULL;
    if (index < 0 || index > 98)
        return; /* Explicit bound for a corrupted encoder state. */
    if (index == 3)
        bitmap = selected ? pico_ui_editor_backspace_large : pico_ui_editor_backspace_small;
    else if (index == 98)
        bitmap = selected ? pico_ui_editor_space_large : pico_ui_editor_space_small;
    else if ((selected && index < 4) || index == 1)
        bitmap = selected ? pico_ui_editor_done_large : pico_ui_editor_done_small;
    if (bitmap) {
        if (selected)
            x = index == 3 || index == 98 ? 62 : 61;
        pico_graphics_bitmap(&pico_ui_active->display, (short)x, 16, bitmap,
                             selected ? 16 : 8, selected ? 16 : 8, selected ? 0 : 1);
    } else if (selected)
        pico_display_character_at_pixels(63, 16, characters[index]);
    else
        pico_display_character_on_row((short)x, 1, characters[index]);
}

void pico_encoder_edit_text(char *text, int capacity)
{
    static const char characters[100] =
        " a aabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ ";
    PicoEncoder *encoder = &pico_ui_active->encoder;
    pico_encoder_set_range(encoder, 0, 98, 4);
    pico_encoder_enable_wrap(encoder);
    pico_display_clear_rows(3, 7);
    unsigned length = 0;
    for (;;) {
        while (!pico_encoder_take_click(encoder)) {
            if (pico_local_cancelled()) {
                encoder->wrap_enabled = 0;
                return;
            }
            int selected = pico_encoder_get_position(encoder), first = (selected + 91) % 99;
            pico_display_clear_row(1);
            for (int slot = 0; slot < 8; slot++)
                editor_character((first + slot) % 99, slot * 7, false, characters);
            for (int slot = 0; slot < 7; slot++)
                editor_character((first + 9 + slot) % 99, 77 + slot * 7, false, characters);
            pico_graphics_fill_rectangle(&pico_ui_active->display, 58, 16, 17, 16, 1);
            pico_graphics_text_color(&pico_ui_active->display, 0);
            editor_character(selected, 0, true, characters);
            pico_graphics_text_color(&pico_ui_active->display, 1);
            pico_display_flush();
        }
        if (pico_local_cancelled()) {
            encoder->wrap_enabled = 0;
            return;
        }
        int selected = pico_encoder_get_position(encoder);
        if (selected == 3) {
            if (length)
                length = (length - 1) & 255;
            if (capacity > 0)
                text[length] = 0;
        } else if (selected < 3) {
            encoder->wrap_enabled = 0;
            return;
        } else if (selected < 99 && (int)length < capacity - 1) {
            text[length] = characters[selected];
            length = (length + 1) & 255;
            text[length] = 0;
        }
        pico_display_clear_row(0);
        pico_display_text_at_cell(0, 0, text + (length > 16 ? length - 16 : 0));
        pico_display_flush();
    }
}

void pico_display_adjustment_box(int style, uint32_t index)
{
    if (index >= 10 || (style != 0 && style != 2))
        return;
    short x = (short)(index * 8 + pico_display_cell_width() * 3 + 2);
    short y = (short)(pico_display_cell_height() + 5);
    pico_graphics_fill_rectangle(&pico_ui_active->display, x, y, 8, 8, 0);
    pico_graphics_bitmap(&pico_ui_active->display, x, y,
        style == 0 ? pico_ui_bitmap_adjustment_empty : pico_ui_bitmap_adjustment_filled, 8, 8, 1);
}

uint32_t pico_encoder_select_adjustment_level(uint32_t baseline, int32_t *stored, bool *changed)
{
    if (*stored == -1)
        *stored = (int32_t)baseline;
    int initial = *stored - 1;
    PicoEncoder *encoder = &pico_ui_active->encoder;
    pico_encoder_set_range(encoder, -1, 9, initial);
    for (int box = 0; box < 10; box++)
        pico_display_adjustment_box(box <= initial ? 2 : 0, (uint32_t)box);
    short marker = baseline ? (short)((short)baseline * 8 + 18) : 26;
    pico_graphics_fill_triangle(&pico_ui_active->display, (short)(marker + 3), 15,
                                (short)(marker - 3), 15, marker, 19, 1);
    pico_display_flush();
    pico_encoder_clear_click(encoder);
    int displayed = initial;
    while (!pico_encoder_take_click(encoder)) {
        if (pico_local_cancelled()) {
            *changed = false;
            return (uint32_t)*stored;
        }
        int position = pico_encoder_get_position(encoder);
        if (position != displayed) {
            displayed += position < displayed ? -1 : 1;
            for (int box = 0; box < 10; box++)
                pico_display_adjustment_box(box <= displayed ? 2 : 0, (uint32_t)box);
            pico_display_flush();
            pico_encoder_set_position(encoder, displayed);
        }
    }
    *changed = initial != pico_encoder_get_position(encoder);
    /* Retain the OEM's separate volatile encoder reads at confirmation. */
    if (pico_encoder_get_position(encoder) < 0 || pico_encoder_get_position(encoder) < 9) {
        if (pico_encoder_get_position(encoder) < 0)
            return 0;
        return (uint8_t)(pico_encoder_get_position(encoder) + 1);
    }
    return 10;
}
