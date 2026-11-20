/* Pico C display wrappers and both controller variants, reconstructed from OEM.
 * Entry addresses and bounded original-instruction comparisons are recorded in
 * analysis/ui-graphics-tests.json. This file performs no physical qualification.
 */
#include "ui_internal.h"

void pico_display_construct(void)
{
    PicoDisplay *d = &pico_ui_active->display;
    d->native_width = d->width = 128;
    d->native_height = d->height = PICO_C_DISPLAY_SSD1309 ? 32 : 64;
    d->cursor_x = d->cursor_y = 0;
    d->foreground = d->background = 0xffff;
    d->text_scale = 1;
    d->rotation = 0;
    d->wrap = 1;
    d->codepage_437 = 0;
    d->command_data_pin = 13;
    d->chip_select_pin = 4;
    d->shared_chip_select_pin = 18;
    d->font = NULL;
}

void pico_display_clear(void)
{
    memset(pico_ui_active->display.framebuffer, 0, PICO_C_DISPLAY_SSD1309 ? 512 : 1024);
}

void pico_display_clear_thunk(void)
{
    pico_display_clear();
}

void pico_display_flush(void)
{
    PicoDisplay *d = &pico_ui_active->display;
#if PICO_C_DISPLAY_SSD1309
    /* Preserve the OEM driver's eight-page read operation in an explicit bounded
     * transfer window. Its original 32-row graphics buffer occupied only 512 bytes;
     * the second half of its read window was adjacent mutable OEM data. A fresh
     * typed UI context initializes that reserved half to zero. This memory-layout
     * repair is explicit; byte-for-byte whole-appliance equivalence is not claimed.
     */
    for (unsigned page = 0; page < 8; page++) {
        pico_oled_command(d, (uint8_t)(0xb0 + page));
        pico_oled_command(d, 0x00);
        pico_oled_command(d, 0x10);
        ui_write(d->chip_select_pin, 1);
        ui_write(d->shared_chip_select_pin, 1);
        ui_write(d->command_data_pin, 1);
        ui_write(d->chip_select_pin, 0);
        ui_write(d->shared_chip_select_pin, 0);
        for (unsigned column = 0; column < 128; column++)
            ui_spi(d->framebuffer[page * 128 + column]);
        ui_write(d->chip_select_pin, 1);
        ui_write(d->shared_chip_select_pin, 1);
    }
#else
    static const uint8_t window[] = {0x15, 0, 63, 0x75, 0, 63};
    for (size_t i = 0; i < sizeof window; i++)
        pico_oled_command(d, window[i]);
    ui_write(d->chip_select_pin, 1);
    ui_write(d->shared_chip_select_pin, 1);
    ui_write(d->command_data_pin, 1);
    ui_write(d->chip_select_pin, 0);
    ui_write(d->shared_chip_select_pin, 0);
    ui_delay(1);
    for (unsigned x = 0; x < 128; x += 2)
        for (unsigned page = 0; page < 1024; page += 128) {
            unsigned left = d->framebuffer[page + x], right = d->framebuffer[page + x + 1];
            for (unsigned bit = 0; bit < 8; bit++)
                ui_spi((uint8_t)(((left >> bit) & 1 ? 0xf0 : 0) |
                                  ((right >> bit) & 1 ? 15 : 0)));
        }
    ui_write(d->chip_select_pin, 1);
    ui_write(d->shared_chip_select_pin, 1);
#endif
}

void pico_display_builtin_font(void)
{
    pico_graphics_font(&pico_ui_active->display, NULL);
}

void pico_display_custom_font(void)
{
    pico_graphics_font(&pico_ui_active->display, &pico_c_ui_custom_font);
}

int pico_display_cell_width(void)
{
    return pico_ui_active->display.font ? 7 : 6;
}

int pico_display_cell_height(void)
{
    return pico_ui_active->display.font ? 16 : 8;
}

void pico_display_initialize(void)
{
    PicoDisplay *d = &pico_ui_active->display;
    pico_oled_initialize(d);
    pico_graphics_text_colors(d, 1, 0);
#if !PICO_C_DISPLAY_SSD1309
    pico_graphics_rotation(d, 2);
    pico_oled_command(d, 0x81);
    pico_oled_command(d, 0x7e);
#endif
    pico_display_custom_font();
    pico_display_clear();
}

void pico_display_text_at_cell(short row, short column, const char *text)
{
    PicoDisplay *d = &pico_ui_active->display;
    pico_graphics_cursor(d, (int16_t)(column * pico_display_cell_width()),
                         (int16_t)(row * pico_display_cell_height()));
    pico_graphics_text(d, text);
}

void pico_display_string_at_cell(short row, short column, const char *text)
{
    pico_display_text_at_cell(row, column, text);
}

void pico_display_center_text(short row, const char *text)
{
    PicoDisplay *d = &pico_ui_active->display;
    int32_t width = (int32_t)((uint32_t)strlen(text) * d->text_scale * pico_display_cell_width());
    int16_t x = width < 128 ? (int16_t)((128 - width) / 2) : 0;
    pico_graphics_cursor(d, x, (int16_t)(row * pico_display_cell_height()));
    pico_graphics_text(d, text);
}

void pico_display_center_text_at_pixels(short y, const char *text)
{
    PicoDisplay *d = &pico_ui_active->display;
    int32_t width = (int32_t)((uint32_t)strlen(text) * d->text_scale * pico_display_cell_width());
    int16_t x = width < 128 ? (int16_t)((128 - width) / 2) : 0;
    pico_graphics_cursor(d, x, y);
    pico_graphics_text(d, text);
}

void pico_display_text_at_pixels(short y, short x, const char *text)
{
    pico_graphics_cursor(&pico_ui_active->display, x, y);
    pico_graphics_text(&pico_ui_active->display, text);
}

void pico_display_character_at_pixels(short x, short y, char character)
{
    pico_graphics_cursor(&pico_ui_active->display, x, y);
    pico_graphics_write(&pico_ui_active->display, (uint8_t)character);
}

void pico_display_clear_row(short row)
{
    PicoDisplay *d = &pico_ui_active->display;
    int cell_height = pico_display_cell_height();
    pico_graphics_fill_rectangle(d, 0, (int16_t)(row * cell_height), 128,
                                (int16_t)(d->text_scale * cell_height), 0);
}

void pico_display_clear_row_from_column(short row, short column)
{
    PicoDisplay *d = &pico_ui_active->display;
    int cell_height = pico_display_cell_height();
    pico_graphics_fill_rectangle(d, (int16_t)(column * pico_display_cell_width()),
                                (int16_t)(row * cell_height), 128,
                                (int16_t)(d->text_scale * cell_height), 0);
}

void pico_display_clear_rows(uint32_t first, uint32_t last)
{
    uint32_t end = last < 8 ? (last + 1) & 255 : 8;
    for (; first < end; first = (first + 1) & 255)
        pico_display_clear_row((short)first);
}
