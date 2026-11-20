#include "pico_c/ui.h"
#include <string.h>

static PicoUi ui;
static PicoAppState app;
static PicoUiHardware hardware;
static PicoUiServices services;
PicoUi *pico_ui_active = &ui;
static uint32_t config[18], positions[32], position_count, read_index;
static uint32_t events[2048][6], event_count, clocks, clicks;

static void event(unsigned kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    if (event_count < 2048) {
        uint32_t values[] = {kind, a, b, c, d, e};
        memcpy(events[event_count++], values, sizeof values);
    }
}
static uint32_t millis(void *context)
{
    (void)context;
    uint32_t value = config[11] + clocks++ * config[12];
    event(1, value, 0, 0, 0, 0);
    return value;
}
static void redraw(void) { event(2, 0, 0, 0, 0, 0); }
static void pause(void *context) { (void)context; event(3, 0, 0, 0, 0, 0); }
void pico_network_assert(PicoNetwork *n, bool valid, const char *expression)
{ (void)n; (void)valid; (void)expression; }
void pico_encoder_set_range(PicoEncoder *e, int minimum, int maximum, int initial)
{ (void)e; event(4, minimum, maximum, initial, 0, 0); }
int pico_encoder_get_position(PicoEncoder *e)
{
    (void)e;
    unsigned index = read_index++;
    uint32_t value = positions[index < position_count ? index : position_count - 1];
    event(5, value, 0, 0, 0, 0);
    return (int32_t)value;
}
void pico_encoder_set_position(PicoEncoder *e, int value)
{ (void)e; event(6, value, 0, 0, 0, 0); }
void pico_encoder_clear_click(PicoEncoder *e)
{ (void)e; event(7, 0, 0, 0, 0, 0); }
int pico_encoder_take_click(PicoEncoder *e)
{
    (void)e;
    int value = ++clicks >= config[13];
    event(8, value, 0, 0, 0, 0);
    return value;
}
void pico_display_clear(void) { event(9, 0, 0, 0, 0, 0); }
void pico_display_flush(void) { event(10, 0, 0, 0, 0, 0); }
void pico_display_update_scrolling(void) { event(11, 0, 0, 0, 0, 0); }
void pico_display_stop_scrolling(void) { ui.scrolling.enabled = 0; }
bool pico_filter_is_inserted(PicoActuators *a)
{ (void)a; event(12, config[10], 0, 0, 0, 0); return config[10] != 0; }
bool pico_still_blocked(PicoNetwork *n)
{ (void)n; event(13, config[14], 0, 0, 0, 0); return config[14] != 0; }
void pico_ui_draw_menu_items_direction(uint32_t n, int selected, int first,
                                        uint32_t visible, int direction)
{ event(14, n, selected, first, visible, (uint32_t)direction); }
/* Unused by the choose entry; their owning data module has its own oracle. */
uint32_t pico_menu_action_at(PicoMenu *m, int i) { return m->actions[i]; }
void pico_label_no(char *destination) { strcpy(destination, "No"); }
void pico_label_yes(char *destination) { strcpy(destination, "Yes"); }

uint32_t pico_test_menu_run(const uint32_t settings[18], const uint32_t *readings, unsigned count)
{
    memset(&ui, 0, sizeof ui);
    memset(&app, 0, sizeof app);
    memcpy(config, settings, sizeof config);
    position_count = count > 32 ? 32 : count;
    memcpy(positions, readings, position_count * sizeof *readings);
    event_count = clocks = clicks = read_index = 0;
    hardware.millis = millis;
    services.pause_until_resume = pause;
    ui.app = &app;
    ui.hardware = &hardware;
    ui.services = &services;
    ui.scrolling.enabled = 1;
    app.controller.phase = (uint8_t)config[8];
    app.controller.status = (uint8_t)config[9];
    app.carousel.pages[0] = config[15] ? redraw : NULL;
    return pico_menu_choose_with_timeout(config[0], config[1], config[2], config[3],
                                          (uint8_t)config[4], (uint8_t)config[5], config[6]);
}
uint32_t pico_test_menu_events(void) { return event_count; }
uint32_t pico_test_menu_event(unsigned index, unsigned word) { return events[index][word]; }
uint32_t pico_test_menu_scrolling(void) { return ui.scrolling.enabled; }
