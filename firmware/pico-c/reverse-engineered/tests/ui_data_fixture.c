#include "pico_c/ui.h"
#include <string.h>

static PicoUi ui;
static PicoAppState app;
static PicoNetwork network;
static PicoSettings settings;
PicoUi *pico_ui_active = &ui;

/* Logging is outside the data/menu functions under comparison. */
void pico_network_log(PicoNetwork *n, const char *l, const char *c, const char *m)
{ (void)n; (void)l; (void)c; (void)m; }
void pico_network_assert(PicoNetwork *n, bool valid, const char *expression)
{ (void)n; (void)valid; (void)expression; }

void pico_test_data_reset(unsigned language)
{
    memset(&ui, 0, sizeof ui);
    memset(&app, 0, sizeof app);
    memset(&network, 0, sizeof network);
    memset(&settings, 0, sizeof settings);
    ui.app = &app;
    ui.network = &network;
    network.settings = &settings;
    settings.language = (uint8_t)language;
    memset(ui.menu_labels, 0x5a, sizeof ui.menu_labels);
}
const void *pico_test_data_labels(void) { return ui.menu_labels; }
const void *pico_test_data_menu(void) { return &app.menu; }
