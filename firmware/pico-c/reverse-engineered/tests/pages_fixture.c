/* Native page-composition fixture; never part of firmware. Two explicit
 * service boundaries isolate the temperature-summary and state-label helpers. */
#include "pico_c/ui.h"
#include <string.h>
extern const PicoBitmap pico_ui_asset_program_primary;
extern const PicoBitmap pico_ui_asset_program_secondary;

static uint32_t events[16][8], count;
void __wrap_pico_display_temperature_summary(double wort, double steam, double target, short row)
{
    if (count >= 16) return;
    uint32_t *event = events[count++];
    event[0] = 1;
    memcpy(event + 1, &wort, 8);
    memcpy(event + 3, &steam, 8);
    memcpy(event + 5, &target, 8);
    event[7] = (uint32_t)(int32_t)row;
}
const char *__wrap_pico_brew_display_state_label(uint32_t phase, uint32_t language)
{
    if (count < 16) {
        events[count][0] = 2; events[count][1] = phase; events[count++][2] = language;
    }
    return "STATE";
}
void pico_test_pages_configure(const int32_t config[10], const double values[8])
{
    PicoUi *ui = pico_ui_context();
    PicoAppState *app = ui->app;
    PicoSettings *settings = ui->network->settings;
    settings->detailed_status = (uint8_t)config[0];
    settings->language = (uint8_t)config[1];
    settings->temperature_unit_celsius = (uint8_t)config[2];
    app->controller.phase = (uint8_t)config[3];
    app->recipe.current_step = config[4];
    app->screen.heated_hold = (uint8_t)config[5];
    app->screen.progress_component = (uint32_t)config[6];
    app->controller.step_seconds = config[7];
    app->controller.still.temperature_units = (uint8_t)config[8];
    app->session.session_type = (uint32_t)config[9];
    app->recipe.program = &app->session;
    app->session.logo_text = "NOW BREWING";
    app->session.bitmap_primary = &pico_ui_asset_program_primary;
    app->session.bitmap_secondary = &pico_ui_asset_program_secondary;
    app->controller.wort_fahrenheit = values[0];
    app->controller.thermoblock_fahrenheit = values[1];
    app->controller.wort_target_fahrenheit = values[2];
    app->recipe.decimal_metric = values[3]; app->recipe.integer_metric = 37;
    app->controller.still.temperature1 = values[4];
    app->controller.still.temperature2 = values[5];
    app->controller.still.temperature3 = values[6];
    app->controller.still.pressure = values[7];
    app->screen.brew_state = 2;
    strcpy(app->screen.recipe_name, "Test Brew"); strcpy(app->screen.step_name, "Mash");
    strcpy(app->controller.phase_label, "Heating");
    for (unsigned i = 0; i < 1024; ++i)
        ui->network->recipe_images[0][i] = (uint8_t)(i * 13u + 7u);
    memset(events, 0, sizeof events); count = 0;
}
void pico_test_pages_run(unsigned operation)
{
    PicoUi *ui = pico_ui_context();
    switch (operation) {
    case 0: pico_screen_brew_name_page(); break;
    case 1: pico_screen_current_step_page(); break;
    case 2: pico_screen_recipe_metrics_page(); break;
    case 3: pico_screen_step_progress_page(); break;
    case 4: pico_screen_temperature_page(); break;
    case 5: pico_screen_program_status_page(); break;
    case 6: pico_screen_basic_brew_status(); break;
    case 7: pico_screen_detailed_brew_status(); break;
    case 8: pico_ui_draw_hold_status(); break;
    case 9: pico_screen_still_readings(); break;
    case 10: pico_screen_manual_overview_page(); break;
    case 11: pico_screen_current_target_page(); break;
    case 12: pico_ui_recipe_logo_ready(ui, ui->network->recipe_logo);
             ui->app->carousel.pages[0](); break;
    case 13: ui->app->carousel.page_count = 1;
             pico_ui_recipe_image_ready(ui, 0, ui->network->recipe_images[0]);
             ui->app->carousel.pages[1](); break;
    case 14: pico_ui_draw_circulation_status(ui->app->controller.wort_fahrenheit,
                                           (uint32_t)ui->app->controller.step_seconds); break;
    }
}
unsigned pico_test_pages_events(void) { return count; }
uint32_t pico_test_pages_event(unsigned i, unsigned j) { return events[i][j]; }
