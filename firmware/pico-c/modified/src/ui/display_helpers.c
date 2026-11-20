/* Shared typed UI state helpers; C entry correspondence in analysis/function-correspondence.json. */
#include "ui_internal.h"

void pico_display_refresh_current_page(void)
{
    pico_display_clear();
    /* The recovered entry always redraws page zero; its historical name is kept.
     */
    pico_ui_active->app->carousel.pages[0]();
    pico_display_flush();
    return;
}

void pico_screen_set_recipe_name(PicoScreenState *screen, const char *recipe_name)
{
    ui_assert(!ui_empty(recipe_name), "!CString::isNullOrEmpty(_recipeName)");
    ui_cstring_copy(screen->recipe_name, sizeof screen->recipe_name, recipe_name);
}

void pico_screen_set_step(PicoScreenState *screen, uint32_t component, const char *step_name)
{
    ui_assert(!ui_empty(step_name), "!CString::isNullOrEmpty(_stepName)");
    ui_cstring_copy(screen->step_name, sizeof screen->step_name, step_name);
    screen->progress_component = component;
    char log_buffer[128];
    snprintf(log_buffer, sizeof log_buffer, "setStep comp = %d stepName = \"%s\"", (int)component,
             step_name);
    ui_log("I", "SCR", log_buffer);
}

void pico_screen_add_lines(PicoTextScreen *screen, int count, ...)
{
    va_list arguments;
    va_start(arguments, count);
    for (int index = 0; index < count; ++index) {
        const char *line = va_arg(arguments, const char *);
        ui_assert(!ui_empty(line), "!CString::isNullOrEmpty(line)");
        if (screen->line_count < PICO_TEXT_LINE_CAPACITY)
            screen->lines[screen->line_count++] = line;
        else
            ui_log("E", "SCREEN", "Too many lines");
    }
    va_end(arguments);
}

void pico_display_advance_carousel(PicoPageCarousel *carousel)
{
    uint32_t now_ms;
    uint32_t next_page;

    now_ms = ui_millis();
    if (carousel->interval_ms < now_ms - carousel->last_update_ms) {
        now_ms = ui_millis();
        carousel->last_update_ms = now_ms;
        next_page = carousel->page_index + 1;
        carousel->page_index = next_page;
        if ((int)carousel->page_count <= (int)next_page) {
            carousel->page_index = 0;
        }
    }
    if (carousel->pages[carousel->page_index] == (PicoPageCallback)0x0) {
        return;
    }
    pico_display_clear();
    (*carousel->pages[carousel->page_index])();
    pico_display_flush();
    return;
}

void pico_display_valve_position(short row, uint32_t position)
{
    char *position_label;

    pico_display_clear_row(row);
    switch (position) {
    case 1:
        position_label = "Mash";
        break;
    case 2:
        position_label = "Pass Thru";
        break;
    case 3:
        position_label = "Adj 1";
        break;
    case 4:
        position_label = "Adj 2";
        break;
    case 5:
        position_label = "Adj 4";
        break;
    case 6:
        position_label = "Adj 3";
        break;
    default:
        position_label = "StopPos";
    }
    pico_display_center_text(row, position_label);
    return;
}

const uint8_t *pico_display_digit_bitmap(int digit)
{
    return pico_ui_digits[digit - 1u < 9 ? (unsigned)digit : 0];
}

char *pico_format_float_decimal(float value, char *destination, uint32_t decimals)
{
    ui_itoa(ui_int(value), destination, 10);
    if (signbit(value))
        value = -value;
    if (decimals) {
        strcat(destination, ".");
        float fraction = value - (float)ui_int(value);
        uint32_t scale = 1;
        for (uint32_t i = 0; i < decimals; i++)
            scale *= 10;
        fraction *= (float)scale;
        char digits[12];
        ui_itoa((int)ui_uint(fraction), digits, 10);
        size_t length = strlen(digits);
        for (uint32_t i = (uint32_t)length; i < decimals; i++)
            strcat(destination, "0");
        strcat(destination, digits);
    }
    return destination;
}
