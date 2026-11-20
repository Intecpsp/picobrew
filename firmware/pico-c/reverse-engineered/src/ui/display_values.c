/* Pico C text and value layout, recovered from the original two-row UI.
 * Main-image entries:044a0,0452c,0571c,05744,07e54,07eb4,08008,
 * 08d70,09910,09ee4,0a0a0,0a0bc,0a0d8. Prefix2000 is omitted here.
 */
#include "ui_internal.h"

void pico_display_bitmap_at(short x, short y, const PicoBitmap *bitmap)
{
    pico_graphics_bitmap(&pico_ui_active->display, x, y, bitmap->pixels,
                         (short)bitmap->width, (short)bitmap->height, 1);
}

void pico_display_center_bitmap(short y, const PicoBitmap *bitmap)
{
    pico_display_bitmap_at((short)(64 - bitmap->width / 2), y, bitmap);
}

void pico_display_fixed_width_center(short y, const char *text)
{
    int32_t width = (int32_t)((uint32_t)strlen(text) * pico_ui_active->display.text_scale * 7u);
    pico_graphics_cursor(&pico_ui_active->display, (short)(64 - width / 2), y);
    pico_graphics_text(&pico_ui_active->display, text);
}

void pico_display_center_message(const char *text)
{
    pico_display_fixed_width_center(0, text);
}

void pico_display_character_on_row(short x, short row, char character)
{
    pico_display_character_at_pixels(x, (short)(row * pico_display_cell_height()), character);
}

void pico_display_device_bitmap(void)
{
    pico_display_clear();
    pico_display_text_at_cell(0, 0, "WiFi");
    pico_display_text_at_cell(1, 0, "Initalizing");
}

void pico_display_device_bitmap_variant_a(void)
{
    pico_display_clear_row(1);
    pico_display_text_at_cell(1, 0, "Connected!");
}

void pico_display_device_bitmap_variant_b(void)
{
    pico_display_clear_row(1);
    pico_display_text_at_cell(1, 0, "Failed Connect");
}

void pico_display_logo(void)
{
    pico_display_clear_row(0);
    pico_display_center_text(0, "Pico C");
}

void pico_display_draw_titled_frame(const char *title)
{
    pico_display_center_text(0, title);
}

void pico_display_stop_progress_indicator(PicoControlSignals *signals)
{
    unsigned first = signals->progress_start_column;
    signals->progress_active = 0;
    pico_graphics_fill_rectangle(&pico_ui_active->display, (short)(first * 7),
                                 (short)(signals->progress_row * 16),
                                 (short)((signals->progress_end_column - first) * 7), 16, 0);
    pico_display_flush();
}

void pico_display_update_timer(short row, int started_ms, int duration_seconds)
{
    uint32_t now = ui_millis(), remaining;
    uint32_t duration = (uint32_t)duration_seconds * 1000u;
    if (duration_seconds == 0)
        remaining = now - (uint32_t)started_ms;
    else {
        remaining = duration + (uint32_t)started_ms - now;
        if (remaining > duration + 1u)
            remaining = 0;
    }
    char text[24];
    snprintf(text, sizeof text, "%u:%02u", (unsigned)(remaining / 60000),
             (unsigned)((remaining % 60000) / 1000));
    if (pico_display_cell_height() == 16 && (uint32_t)row > 1)
        row = 1;
    pico_display_clear_row(row);
    pico_display_center_text(row, text);
    pico_display_flush();
}

void pico_display_recipe_metrics(void)
{
    char number[24], text[64];
    pico_format_float_decimal((float)pico_ui_active->app->recipe.decimal_metric, number, 1);
    snprintf(text, sizeof text, "%s%% ABV %d IBUs", number,
             (int)pico_ui_active->app->recipe.integer_metric);
    pico_display_center_text(1, text);
}

void pico_display_large_timer_at_row(int minutes, uint32_t seconds, int show_seconds, int row)
{
    char text[16];
    ui_itoa(minutes / 60, text, 10);
    int x = minutes / 60 < 10 ? 36 : 28;
    short y = (short)((uint32_t)row * 16u);
    for (const char *c = text; *c; c++, x += 16)
        pico_graphics_bitmap(&pico_ui_active->display, (short)x, y,
                             pico_display_digit_bitmap(*c - '0'), 16, 16, 1);
    pico_graphics_bitmap(&pico_ui_active->display, (short)x, y, pico_ui_bitmap_colon_dot, 8, 16, 1);
    x += 8;
    if (minutes % 60 < 10) {
        pico_graphics_bitmap(&pico_ui_active->display, (short)x, y, pico_ui_digits[0], 16, 16, 1);
        x += 16;
    }
    ui_itoa(minutes % 60, text, 10);
    for (const char *c = text; *c; c++, x += 16)
        pico_graphics_bitmap(&pico_ui_active->display, (short)x, y,
                             pico_display_digit_bitmap(*c - '0'), 16, 16, 1);
    if (show_seconds) {
        if ((int32_t)seconds < 0)
            seconds = 0;
        snprintf(text, sizeof text, seconds < 10 ? ":0%u" : ":%u", (unsigned)seconds);
        pico_display_builtin_font();
        pico_display_text_at_pixels(y, (short)x, text);
        pico_display_custom_font();
    }
}

void pico_display_large_timer(int minutes, uint32_t seconds, int show_seconds)
{
    pico_display_large_timer_at_row(minutes, seconds, show_seconds, 0);
}

void pico_display_large_timer_with_seconds(int minutes, uint32_t seconds)
{
    pico_display_large_timer_at_row(minutes, seconds, 1, 1);
}

void pico_display_large_temperature_at_row(double fahrenheit, int show_decimal, int row)
{
    double value = ui_display_temperature(fahrenheit);
    int digits = value > 99.9 ? 4 : value > 9.9 ? 3 : 2;
    if (!show_decimal)
        digits--;
    int width = digits * 16 + (show_decimal ? 15 : 7);
    int x = width < 128 ? (128 - width) / 2 : 0;
    if (!show_decimal)
        x += 4;
    short y = (short)((uint32_t)row * 16u);
    char text[24];
    pico_format_float_decimal((float)value, text, 1);
    for (unsigned i = 0; text[i] && i < 5; i++) {
        if (text[i] == '.') {
            if (!show_decimal)
                break;
            pico_graphics_bitmap(&pico_ui_active->display, (short)x, (short)(y + 9),
                                 pico_ui_bitmap_colon_dot, 8, 8, 1);
            x += 8;
        } else {
            pico_graphics_bitmap(&pico_ui_active->display, (short)x, y,
                                 pico_display_digit_bitmap(text[i] - '0'), 16, 16, 1);
            x += 16;
        }
    }
    pico_display_builtin_font();
    pico_display_text_at_pixels(y, (short)x,
        pico_ui_active->network->settings->temperature_unit_celsius ? "C" : "F");
    pico_display_custom_font();
}

void pico_display_large_temperature(double fahrenheit, int show_decimal)
{
    pico_display_large_temperature_at_row(fahrenheit, show_decimal, 0);
}

void pico_display_large_temperature_with_decimal(double fahrenheit)
{
    pico_display_large_temperature_at_row(fahrenheit, 1, 0);
}

void pico_display_temperature_columns(short row, double wort, double steam, double target)
{
    int goal = ui_int(ui_display_temperature(target));
    bool celsius = pico_ui_active->network->settings->temperature_unit_celsius != 0;
    if (celsius && goal < 0)
        goal = 0;
    char text[64], target_text[16];
    if (goal == 0)
        strcpy(target_text, "-");
    else
        ui_itoa(goal, target_text, 10);
    snprintf(text, sizeof text, "w%d s%d g%s%c", ui_int(ui_display_temperature(wort)),
             ui_int(ui_display_temperature(steam)), target_text, celsius ? 'C' : 'F');
    short actual_row = row != 0;
    pico_display_clear_row(actual_row);
    pico_display_center_text(actual_row, text);
}

void pico_display_temperature_summary(double wort, double steam, double target, uint16_t row)
{
    pico_display_temperature_columns((short)row, wort, steam, target);
}
