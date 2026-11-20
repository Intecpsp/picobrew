#include "pico_c/ui.h"
#include <string.h>
#include <stdio.h>

static PicoUi ui;
static PicoAppState app;
static PicoNetwork network;
static PicoSettings settings;
static uint32_t events[32768][3];
static uint32_t event_count;
static uint32_t now_ms;
static void event(uint32_t kind, uint32_t a, uint32_t b)
{
    if (event_count < 32768) {
        events[event_count][0] = kind;
        events[event_count][1] = a;
        events[event_count++][2] = b;
    }
}
static void delay_ms(void *ctx, uint32_t value) { (void)ctx; event(1, value, 0); }
static void pin_mode(void *ctx, unsigned pin, unsigned mode) { (void)ctx; event(2, pin, mode); }
static void pin_write(void *ctx, unsigned pin, unsigned value) { (void)ctx; event(3, pin, value); }
static void spi_begin(void *ctx) { (void)ctx; event(4, 0, 0); }
static uint8_t spi_transfer(void *ctx, uint8_t value) { (void)ctx; event(5, value, 0); return 0; }
static uint32_t millis(void *ctx) { (void)ctx; event(6, now_ms, 0); return now_ms; }
static void redraw(void) { event(7, 0, 0); }
static uint32_t text_hash(const char *text)
{
    uint32_t value = 0;
    while (*text) value = value * 33u + (uint8_t)*text++;
    return value;
}
void pico_network_log(PicoNetwork *network, const char *level, const char *component,
                       const char *message)
{
    (void)network;
    event(8, text_hash(level) ^ text_hash(component), text_hash(message));
}
void pico_network_assert(PicoNetwork *n, bool condition, const char *expression)
{ (void)n; (void)condition; (void)expression; }
static const PicoUiHardware hardware = {
    .millis = millis,
    .delay_ms = delay_ms, .pin_mode = pin_mode, .digital_write = pin_write,
    .spi_begin = spi_begin, .spi_transfer = spi_transfer
};

void pico_test_ui_reset(int custom, int rotation, int scale, int wrap, int x, int y,
                        int foreground, int background, unsigned seed)
{
    memset(&ui, 0, sizeof ui);
    memset(&app, 0, sizeof app);
    memset(&network, 0, sizeof network);
    memset(&settings, 0, sizeof settings);
    ui.app = &app;
    ui.network = &network;
    network.settings = &settings;
    ui.hardware = &hardware;
    pico_ui_bind(&ui);
    pico_display_construct();
    pico_graphics_rotation(&ui.display, (uint8_t)rotation);
    ui.display.text_scale = (uint8_t)scale;
    ui.display.wrap = (uint8_t)wrap;
    ui.display.foreground = (uint16_t)foreground;
    ui.display.background = (uint16_t)background;
    if (custom)
        pico_display_custom_font();
    pico_graphics_cursor(&ui.display, (int16_t)x, (int16_t)y);
    for (unsigned i = 0; i < sizeof ui.display.framebuffer; i++)
        ui.display.framebuffer[i] = (uint8_t)(i * 37u + seed);
    for (unsigned i = 0; i < PICO_MENU_CAPACITY; i++)
        snprintf(ui.menu_labels[i], PICO_MENU_LABEL_LENGTH, "Menu item %u", i);
    event_count = 0;
}

void pico_test_values_configure(int celsius, uint32_t now, double metric, int integer_metric)
{
    settings.temperature_unit_celsius = (uint8_t)celsius;
    now_ms = now;
    app.recipe.decimal_metric = metric;
    app.recipe.integer_metric = integer_metric;
}

void pico_test_scroll_configure(const char *text, int row, int pixel, int character, int frame,
                               uint32_t deadline, int enabled, int tick, int hold, int callback)
{
    PicoScrollingText *s = &ui.scrolling;
    size_t length = strlen(text);
    if (length > 251) length = 251;
    memcpy(s->text, text, length);
    s->text[length] = 0;
    s->length = (int32_t)length;
    s->row = row;
    s->pixel_offset = pixel;
    s->character_offset = character;
    s->frame = frame;
    s->deadline_ms = deadline;
    s->enabled = (uint8_t)enabled;
    s->tick_ms = tick;
    s->hold_ms = hold;
    s->redraw = callback ? redraw : NULL;
}

void pico_test_ui_run(unsigned operation, int a, int b, int c, const char *text)
{
    switch (operation) {
    case 0: pico_graphics_write(&ui.display, (uint8_t)a); break;
    case 1: pico_display_text_at_cell((short)a, (short)b, text); break;
    case 2: pico_display_center_text((short)a, text); break;
    case 3: if (a) pico_display_custom_font(); else pico_display_builtin_font(); break;
    case 4: pico_display_clear_row((short)a); break;
    case 5: pico_display_clear_row_from_column((short)a, (short)b); break;
    case 6: pico_graphics_pixel(&ui.display, a, b, c); break;
    case 7: pico_display_clear(); break;
    case 8: pico_display_flush(); break;
    case 9: pico_display_initialize(); break;
    case 10: pico_oled_invert(&ui.display, a != 0); break;
    case 11: pico_display_start_scrolling(text, a ? redraw : NULL); break;
    case 12: now_ms = (uint32_t)a; pico_display_update_scrolling(); break;
    case 13: pico_display_stop_scrolling(); break;
    case 14: pico_display_clear_scrolling(); break;
    case 15: pico_ui_draw_menu_items_direction((uint32_t)a, b, 0, 0, c); break;
    case 16: pico_ui_draw_footer_option(text, a); break;
    case 17: pico_display_logo(); break;
    case 18: pico_display_device_bitmap(); break;
    case 19: if (a) pico_display_device_bitmap_variant_a(); else pico_display_device_bitmap_variant_b(); break;
    case 20: pico_display_fixed_width_center((short)a, text); break;
    case 21: pico_display_character_on_row((short)a, (short)b, (char)c); break;
    case 22: pico_display_update_timer((short)a, b, c); break;
    case 23: pico_display_large_timer_at_row(a, (uint32_t)b, c & 1, c >> 1); break;
    case 24: pico_display_large_temperature_at_row((double)a / 10.0, b, c); break;
    case 25: pico_display_recipe_metrics(); break;
    }
}

const uint8_t *pico_test_ui_framebuffer(void) { return ui.display.framebuffer; }
int pico_test_ui_state(unsigned index)
{
    switch (index) {
    case 0: return ui.display.cursor_x;
    case 1: return ui.display.cursor_y;
    case 2: return ui.display.width;
    case 3: return ui.display.height;
    case 4: return ui.display.font != NULL;
    case 5: return ui.display.rotation;
    case 6: return ui.display.foreground;
    case 7: return ui.display.background;
    default: return -1;
    }
}
unsigned pico_test_ui_events(void) { return event_count; }
uint32_t pico_test_ui_event(unsigned index, unsigned word) { return events[index][word]; }
int32_t pico_test_scroll_state(unsigned index)
{
    PicoScrollingText *s = &ui.scrolling;
    switch (index) {
    case 0: return s->redraw != NULL;
    case 1: return s->length;
    case 2: return s->row;
    case 3: return s->pixel_offset;
    case 4: return s->character_offset;
    case 5: return s->frame;
    case 6: return (int32_t)s->deadline_ms;
    case 7: return s->enabled;
    case 8: return s->tick_ms;
    case 9: return s->hold_ms;
    default: return -1;
    }
}
const char *pico_test_scroll_text(void) { return ui.scrolling.text; }
const char *pico_test_scroll_visible(void) { return ui.scrolling.visible; }
