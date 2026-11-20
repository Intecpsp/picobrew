#include "pico_c/ui.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static PicoUi ui;
static PicoAppState app;
static PicoNetwork network;
static PicoSettings settings;
PicoUi *pico_ui_active = &ui;
static unsigned event_count, filter_inserted;
static struct { uint32_t kind, a, b; char text[512]; } events[128];
static void event(unsigned kind, uint32_t a, uint32_t b, const char *text)
{
    if (event_count >= 128) return;
    events[event_count].kind = kind;
    events[event_count].a = a;
    events[event_count].b = b;
    snprintf(events[event_count++].text, 512, "%s", text ? text : "");
}
void pico_display_clear(void) { event(1, 0, 0, NULL); }
void pico_display_flush(void) { event(2, 0, 0, NULL); }
void pico_display_center_text(short row, const char *text) { event(3, row, 0, text); }
void pico_display_text_at_cell(short row, short col, const char *text) { event(4, row, col, text); }
void pico_display_clear_row(short row) { event(5, row, 0, NULL); }
void pico_display_start_scrolling(const char *text, void (*callback)(void))
{
    unsigned id = callback == pico_ui_draw_stop_footer ? 1 :
                  callback == pico_ui_draw_confirm_footer ? 2 :
                  callback == pico_ui_draw_continue_footer ? 3 : 0;
    event(6, id, 0, text);
    ui.scrolling.enabled = 1;
}
void pico_display_fixed_width_center(short row, const char *text) { event(7, row, 0, text); }
static void disable(void *ctx) { (void)ctx; event(8, 0, 0, NULL); }
static bool enable(void *ctx) { (void)ctx; event(9, 0, 0, NULL); return false; }
bool pico_filter_is_inserted(PicoActuators *actuators)
{ (void)actuators; event(10, filter_inserted, 0, NULL); return filter_inserted != 0; }
static void delay(void *ctx, uint32_t value) { (void)ctx; event(11, value, 0, NULL); }
void pico_ui_draw_footer_single(const char *text) { event(12, 0, 0, text); }
void pico_network_log(PicoNetwork *n, const char *level, const char *component, const char *message)
{
    (void)n;
    char text[512];
    snprintf(text, sizeof text, "%s|%s|%s", level, component, message);
    event(13, 0, 0, text);
}
void pico_network_assert(PicoNetwork *n, bool condition, const char *expression)
{ (void)n; (void)condition; (void)expression; }
void pico_display_draw_titled_frame(const char *text) { pico_display_center_text(0, text); }
void pico_display_logo(void) { pico_display_clear_row(0); pico_display_center_text(0, "Pico C"); }
void pico_display_wait_for_click(void) { event(14, 0, 0, NULL); }
void pico_display_stop_scrolling(void) { ui.scrolling.enabled = 0; }
uint32_t pico_menu_choose(uint32_t row, uint32_t count, uint32_t initial)
{ event(16, row, count, NULL); return (uint32_t)initial; }
uint32_t pico_ui_wait_for_continue(void) { event(17, 0, 0, NULL); return 0; }
static void mac_address(void *ctx, uint8_t *out)
{
    (void)ctx;
    static const uint8_t mac[6] = {0x02, 0x1a, 0x00, 0xff, 0x08, 0xab};
    memcpy(out, mac, 6);
    event(18, 0, 0, NULL);
}
void pico_ui_wait_for_footer_press(const char *label) { event(19, 0, 0, label); }
void pico_screen_add_lines(PicoTextScreen *screen, int count, ...)
{
    va_list arguments;
    va_start(arguments, count);
    while (count-- > 0 && screen->line_count < PICO_TEXT_LINE_CAPACITY)
        screen->lines[screen->line_count++] = va_arg(arguments, const char *);
    va_end(arguments);
}
static const PicoUiHardware hardware = {
    .interrupts_disable = disable, .interrupts_enable = enable, .delay_ms = delay
};
static const PicoNetworkPlatform platform = { .mac_address = mac_address };
void pico_test_screen_reset(unsigned language, unsigned celsius, unsigned filter, unsigned scrolling)
{
    memset(&ui, 0, sizeof ui);
    memset(&app, 0, sizeof app);
    memset(&network, 0, sizeof network);
    memset(&settings, 0, sizeof settings);
    ui.app = &app;
    ui.network = &network;
    ui.hardware = &hardware;
    network.settings = &settings;
    network.platform = &platform;
    settings.language = (uint8_t)language;
    settings.temperature_unit_celsius = (uint8_t)celsius;
    app.recipe.program = &app.session;
    app.session.name = "Recipe";
    app.session.state_label = "Brewing";
    app.session.interrupted_text = "Paused session";
    app.session.temperature = 149;
    app.session.duration = 3661;
    strcpy(app.screen.recipe_name, "Recipe");
    strcpy(app.device_uid, "PICO-001");
    memset(ui.menu_labels, 'Z', sizeof ui.menu_labels);
    filter_inserted = filter;
    ui.scrolling.enabled = (uint8_t)scrolling;
    event_count = 0;
}
unsigned pico_test_screen_events(void) { return event_count; }
uint32_t pico_test_screen_event(unsigned index, unsigned word)
{
    return word == 0 ? events[index].kind : word == 1 ? events[index].a : events[index].b;
}
const char *pico_test_screen_text(unsigned index) { return events[index].text; }
const char *pico_test_screen_labels(void) { return ui.menu_labels[0]; }
unsigned pico_test_screen_state(unsigned index)
{
    PicoControlSignals *s = &app.control_signals;
    switch (index) {
    case 0: return ui.scrolling.enabled;
    case 1: return s->progress_row;
    case 2: return s->progress_start_column;
    case 3: return s->progress_end_column;
    case 4: return s->progress_glyph_index;
    case 5: return s->progress_active;
    default: return 0;
    }
}
