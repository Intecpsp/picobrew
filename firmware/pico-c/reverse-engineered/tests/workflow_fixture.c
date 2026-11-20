#include "pico_c/ui.h"
#define API __attribute__((visibility("default")))
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static PicoUi ui;
static PicoAppState app;
static PicoNetwork network;
static PicoSettings settings;
PicoUi *pico_ui_active = &ui;
static unsigned event_count, filter_inserted, selected, selection_count;
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
                  callback == pico_ui_draw_continue_footer ? 3 :
                  callback == pico_ui_draw_ok_footer ? 4 :
                  callback == pico_ui_draw_start_flush_footer ? 5 :
                  callback == pico_ui_draw_start_transfer_footer ? 6 :
                  callback == pico_ui_draw_start_rinse_footer ? 7 :
                  callback == pico_ui_draw_start_clean_footer ? 8 :
                  callback == pico_ui_draw_done_footer ? 9 : 0;
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
void pico_screen_add_lines(PicoTextScreen *screen, int count, ...)
{
    va_list arguments;
    va_start(arguments, count);
    while (count-- > 0 && screen->line_count < PICO_TEXT_LINE_CAPACITY)
        screen->lines[screen->line_count++] = va_arg(arguments, const char *);
    va_end(arguments);
}

void pico_display_stop_scrolling(void) { ui.scrolling.enabled = 0; }
void pico_display_clear_scrolling(void) { pico_display_stop_scrolling(); pico_display_clear(); }
void pico_ui_show_scrolling_message(const char *text, void (*callback)(void))
{
    unsigned previous = event_count;
    pico_display_start_scrolling(text, callback);
    events[previous].kind = 14;
    pico_display_stop_scrolling();
}
uint32_t pico_menu_choose(uint32_t row, uint32_t count, uint32_t initial)
{
    char text[64]; snprintf(text, sizeof text, "%u", initial);
    event(15, row, count, text);
    ++selection_count;
    return selected;
}
void pico_api_report_state(PicoNetwork *n)
{ (void)n; event(16, app.device_state, 0, NULL); }
void pico_encoder_clear_click(PicoEncoder *e) { (void)e; event(17, 0, 0, NULL); }
bool pico_ui_confirm_at_row(uint32_t row)
{ event(18, row, selected != 0, NULL); return selected != 0; }
static void wait_filter(void *c) { (void)c; event(20, 0, 0, NULL); }
void __wrap_pico_screen_still_setup_step(uint8_t *step) { event(21, *step, 0, NULL); }
uint32_t pico_menu_populate_still_actions(int program)
{ event(22, program, 0, NULL); return 3; }
bool pico_api_error(PicoNetwork *n, int code) { (void)n; event(23, code, 0, NULL); return true; }
static void alarm(void *c) { (void)c; event(24, 0, 0, NULL); }
void pico_actuator_steam_on(PicoActuators *a) { (void)a; event(25, 1, 0, NULL); }
void pico_actuator_steam_off(PicoActuators *a) { (void)a; event(25, 2, 0, NULL); }
void pico_actuator_drain_on(PicoActuators *a) { (void)a; event(25, 3, 0, NULL); }
void pico_actuator_drain_off(PicoActuators *a) { (void)a; event(25, 4, 0, NULL); }
void __wrap_pico_screen_water_error_help(void) { event(26, 0, 0, NULL); ui.scrolling.enabled = 1; }
void __wrap_pico_screen_thermal_recovery_help(void) { event(26, 1, 0, NULL); ui.scrolling.enabled = 1; }
static const PicoUiServices services = {.wait_for_filter=wait_filter, .alarm_until_press=alarm};

static const PicoUiHardware hardware = {
    .interrupts_disable = disable, .interrupts_enable = enable, .delay_ms = delay
};
API void pico_test_workflow_reset(unsigned language, unsigned session, unsigned filter, unsigned choice)
{
    memset(&ui, 0, sizeof ui);
    memset(&app, 0, sizeof app);
    memset(&network, 0, sizeof network);
    memset(&settings, 0, sizeof settings);
    ui.app = &app;
    ui.network = &network;
    ui.hardware = &hardware;
    ui.services = &services;
    network.settings = &settings;
    settings.language = (uint8_t)language;
    settings.temperature_unit_celsius = 0;
    app.session.session_type = session;
    app.session.title = "TITLE";
    app.device_state = 17;
    selected = choice; selection_count = 0;
    app.recipe.program = &app.session;
    app.session.name = "Recipe";
    app.session.state_label = "Brewing";
    app.session.interrupted_text = "Paused session";
    app.session.temperature = 149;
    app.session.duration = 3661;
    strcpy(app.screen.recipe_name, "Recipe");
    memset(ui.menu_labels, 'Z', sizeof ui.menu_labels);
    filter_inserted = filter;
    ui.scrolling.enabled = 1;
    event_count = 0;
}
API unsigned pico_test_workflow_events(void) { return event_count; }
API uint32_t pico_test_workflow_event(unsigned index, unsigned word)
{
    return word == 0 ? events[index].kind : word == 1 ? events[index].a : events[index].b;
}
API const char *pico_test_workflow_text(unsigned index) { return events[index].text; }
API const char *pico_test_workflow_labels(void) { return ui.menu_labels[0]; }
API unsigned pico_test_workflow_state(unsigned index)
{
    PicoControlSignals *s = &app.control_signals;
    switch (index) {
    case 0: return ui.scrolling.enabled;
    case 1: return s->progress_row;
    case 2: return s->progress_start_column;
    case 3: return s->progress_end_column;
    case 4: return s->progress_glyph_index;
    case 5: return s->progress_active;
    case 6: return app.device_state;
    default: return 0;
    }
}

API int pico_test_workflow_run(unsigned operation, unsigned value)
{
    switch (operation) {
    case 0: pico_ui_prompt_deep_clean_setup(); break;
    case 1: pico_ui_require_first_rinse(); break;
    case 2: return pico_ui_prompt_vacuum((int)value);
    case 3: pico_ui_manual_network_step((int)value); break;
    case 4: pico_ui_prompt_picopak_association(); break;
    case 5: pico_ui_show_rinse_finished(); break;
    case 6: pico_ui_first_rinse_step(value); break;
    case 7: pico_ui_rack_beer_step(value); break;
    case 8: return pico_ui_descale_step(value);
    case 9: pico_ui_wait_for_filter_insertion(); break;
    case 10: return pico_ui_confirm_continue_or_abort();
    case 11: return (int)pico_ui_walk_program_steps((int)value);
    case 12: return pico_ui_confirm_thermal_recovery((int)value);
    case 13: return pico_ui_confirm_screen(pico_ui_draw_ok_footer);
    case 14: return pico_ui_choose_binary_option("TITLE");
    case 15: pico_ui_wait_for_stop(); break;
    case 16: return (int)pico_ui_wait_for_continue();
    }
    return 0;
}
