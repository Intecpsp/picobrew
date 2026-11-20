/* Pico C page callbacks reconstructed from the two pinned OEM images.
 * Entry/literal provenance is recorded in analysis/manual-entry-points.json.
 * These two-row application layouts are shared by the C display variants,
 * including the SSD1325 controller with its larger physical framebuffer.
 */
#include "ui_internal.h"

static void temperature_summary(short row)
{
    const PicoBrewController *c = &pico_ui_active->app->controller;
    pico_display_temperature_summary(c->wort_fahrenheit, c->thermoblock_fahrenheit,
                                     c->wort_target_fahrenheit, row);
}

/* Main 0x2000a6d0; alternate 0x2000a6b8. */
void pico_screen_brew_name_page(void)
{
    PicoAppState *app = pico_ui_active->app;
    pico_display_center_text(0, app->screen.recipe_name);
    if (pico_ui_active->network->settings->detailed_status)
        temperature_summary(1);
    else
        pico_display_center_text(1, app->recipe.program->logo_text);
}

/* Main 0x2000a7c8; alternate 0x2000a7b0. */
void pico_screen_recipe_metrics_page(void)
{
    pico_display_recipe_metrics();
    if (pico_ui_active->network->settings->detailed_status)
        temperature_summary(0);
    else
        pico_display_center_text(0, pico_ui_active->app->screen.step_name);
}

/* Main 0x2000479c; alternate 0x200047a0. Invalid array indices are rejected
 * here instead of preserving the OEM's out-of-bounds negative-index read. */
void pico_screen_step_progress_page(void)
{
    unsigned component = pico_ui_active->app->screen.progress_component;
    if (component < 8)
        pico_graphics_bitmap(&pico_ui_active->display, 0, 0,
                             pico_ui_setup_images[component], 128, 30, 1);
}

static void current_timer(void)
{
    int seconds = pico_ui_active->app->controller.step_seconds;
    pico_display_large_timer_at_row(seconds / 60, seconds % 60, 1, 1);
    if (pico_ui_active->network->settings->detailed_status)
        temperature_summary(0);
    else
        pico_display_center_text(0, pico_ui_active->app->screen.step_name);
}

/* Main 0x2000a728; alternate 0x2000a710. */
void pico_screen_current_step_page(void)
{
    current_timer();
}

/* Main 0x20008ed0; alternate 0x20008eb8. */
void pico_screen_temperature_page(void)
{
    const PicoBrewController *c = &pico_ui_active->app->controller;
    pico_display_center_text(0, c->phase_label);
    pico_display_large_temperature_at_row(c->wort_fahrenheit, 1, 1);
}

/* Main 0x2000a790; alternate 0x2000a778. */
void pico_screen_program_status_page(void)
{
    PicoAppState *app = pico_ui_active->app;
    if (app->screen.heated_hold)
        current_timer();
    else {
        pico_display_center_text(0, app->screen.step_name);
        pico_display_center_text(1, pico_brew_display_state_label(
            app->screen.brew_state, pico_ui_active->network->settings->language));
    }
}

static void target_line(char *buffer, size_t capacity, bool localized)
{
    PicoBrewController *c = &pico_ui_active->app->controller;
    PicoSettings *settings = pico_ui_active->network->settings;
    static const char *const arrows[] = {" --> TARGET:", " ->OBJETIVO:", " ---> CIBLE:"};
    double target = c->wort_target_fahrenheit;
    if (target != 0.0)
        target = ui_display_temperature(target);
    unsigned language = settings->language;
    const char *arrow = localized ? arrows[language < 3 ? language : 0] : " --> ";
    char unit = settings->temperature_unit_celsius ? 'C' : 'F';
    snprintf(buffer, capacity, "%d%c%s%d%c", ui_int(ui_display_temperature(c->wort_fahrenheit)),
             unit, arrow, ui_int(target), unit);
}

/* Main 0x20005508; alternate 0x200054f0. */
void pico_screen_basic_brew_status(void)
{
    PicoAppState *app = pico_ui_active->app;
    const PicoProgramContext *program = app->recipe.program;
    const PicoBitmap *bitmap;
    char line[64];
    target_line(line, sizeof line, false);
    if (app->recipe.current_step == 0 && app->controller.phase == PICO_BREW_PHASE_COOLING) {
        static const char *const warnings[] = {"H20 TOO HOT", "H20 MUY ALTA", "H20 TROP HAUTE"};
        unsigned language = pico_ui_active->network->settings->language;
        pico_display_center_text(0, warnings[language < 3 ? language : 0]);
    } else {
        bitmap = app->recipe.current_step == 0 ? program->bitmap_primary : program->bitmap_secondary;
        if (bitmap)
            pico_graphics_bitmap(&pico_ui_active->display, 0, 0, bitmap->pixels,
                                 bitmap->width, bitmap->height, 1);
    }
    pico_display_center_text(1, line);
}

/* Main 0x20008d78; alternate 0x20008d60. The original formats duration,
 * then overwrites that text twice. Only target temperature reaches row zero. */
void pico_screen_detailed_brew_status(void)
{
    PicoAppState *app = pico_ui_active->app;
    const PicoProgramContext *program = app->recipe.program;
    ui_assert(program->session_type == 2 || program->session_type == 4,
              "sess.type == SessionTypes::SousVide || sess.type == SessionTypes::Coffee");
    char line[64];
    snprintf(line, sizeof line, "%d%c", ui_int(ui_display_temperature(
                 app->controller.wort_target_fahrenheit)),
             pico_ui_active->network->settings->temperature_unit_celsius ? 'C' : 'F');
    int seconds = app->controller.step_seconds;
    pico_display_large_timer_at_row(seconds / 60, seconds % 60, 1, 1);
    pico_display_center_text(0, line);
}

/* Main 0x2000af28; alternate 0x2000af10. */
void pico_ui_draw_hold_status(void)
{
    PicoBrewController *c = &pico_ui_active->app->controller;
    int seconds = c->step_seconds;
    int hours = seconds / 3600;
    int minutes = seconds / 60 - hours * 60;
    if (minutes < 1)
        minutes = 0;
    int remainder = seconds % 60;
    char line[80];
    snprintf(line, sizeof line, "TIME:%d:%s%d:%s%d @ %d%c", hours,
             minutes < 10 ? "0" : "", minutes, remainder < 10 ? "0" : "", remainder,
             ui_int(ui_display_temperature(c->wort_target_fahrenheit)),
             pico_ui_active->network->settings->temperature_unit_celsius ? 'C' : 'F');
    pico_display_center_text(0, line);
    pico_ui_draw_footer_option("Drain", 1);
}

/* Main 0x2000b110; alternate 0x2000b0f8. */
void pico_ui_draw_circulation_status(double wort, uint32_t seconds)
{
    char line[32];
    pico_display_clear();
    unsigned first = seconds / 3600, second = (seconds % 3600) / 60;
    if (!first) {
        first = second;
        second = seconds % 60;
    }
    snprintf(line, sizeof line, "%3d%c %02d:%02d", ui_int(ui_display_temperature(wort)),
             pico_ui_active->network->settings->temperature_unit_celsius ? 'C' : 'F',
             (int)first, (int)second);
    pico_display_center_text(0, line);
    pico_ui_draw_footer_single("End Circulate");
    pico_display_flush();
}

/* Main 0x20005d70; alternate 0x20005d58. */
void pico_screen_still_readings(void)
{
    PicoStillTelemetry *still = &pico_ui_active->app->controller.still;
    PicoSettings *settings = pico_ui_active->network->settings;
    double t1 = still->temperature1, t2 = still->temperature2, t3 = still->temperature3;
    if (still->temperature_units != settings->temperature_unit_celsius) {
        if (settings->temperature_unit_celsius) {
            t1 = (t1 - 32.) / 1.8; t2 = (t2 - 32.) / 1.8; t3 = (t3 - 32.) / 1.8;
        } else {
            t1 = t1 * 1.8 + 32.; t2 = t2 * 1.8 + 32.; t3 = t3 * 1.8 + 32.;
        }
    }
    char a[32], b[32], line[80], target[80];
    char unit = settings->temperature_unit_celsius ? 'C' : 'F';
    target_line(target, sizeof target, true);
    pico_display_builtin_font();
    pico_display_center_text(0, "Still Readings");
    pico_format_float_decimal((float)t1, a, 1);
    pico_format_float_decimal((float)t2, b, 1);
    snprintf(line, sizeof line, "T1:%s%c  T2:%s%c", a, unit, b, unit);
    pico_display_center_text(1, line);
    pico_format_float_decimal((float)t3, a, 1);
    pico_format_float_decimal((float)still->pressure, b, 2);
    snprintf(line, sizeof line, "T3:%s%c P:%spsi", a, unit, b);
    pico_display_center_text(2, line);
    pico_display_center_text(3, target);
    pico_display_custom_font();
}

#if !PICO_STANDALONE
static void recipe_logo_page(void)
{
    /* C main 0x2000a724 is a branch into the name page, not a bitmap draw. */
    pico_screen_brew_name_page();
}
static void recipe_image_page(void)
{
    pico_graphics_bitmap(&pico_ui_active->display, 0, 0,
                         pico_ui_active->network->recipe_images[0], 128, 32, 1);
}
void pico_ui_recipe_logo_ready(void *context, const uint8_t image[512])
{
    PicoUi *ui = context;
    if (image != ui->network->recipe_logo)
        memcpy(ui->network->recipe_logo, image, 512);
    ui->app->carousel.pages[0] = recipe_logo_page;
}
void pico_ui_recipe_image_ready(void *context, unsigned index, const uint8_t image[512])
{
    PicoUi *ui = context;
    if (index < 4 && image != ui->network->recipe_images[index])
        memcpy(ui->network->recipe_images[index], image, 512);
    if (ui->app->carousel.page_count < PICO_CAROUSEL_CAPACITY)
        ui->app->carousel.pages[ui->app->carousel.page_count] = recipe_image_page;
}
#else
/* Local recipes have no downloaded artwork. Keep the callback ABI without
 * allocating the legacy network image buffers. */
void pico_ui_recipe_logo_ready(void *context, const uint8_t image[512])
{
    (void)context;
    (void)image;
}
void pico_ui_recipe_image_ready(void *context, unsigned index, const uint8_t image[512])
{
    (void)context;
    (void)index;
    (void)image;
}
#endif
void pico_screen_manual_overview_page(void)
{
    pico_display_center_bitmap((short)((32u - (uint32_t)pico_ui_asset_manual_overview.height) >> 1),
                               &pico_ui_asset_manual_overview);
}
void pico_screen_current_target_page(void)
{
    PicoBrewController *c = &pico_ui_active->app->controller;
    char line[64];
    char unit = pico_ui_active->network->settings->temperature_unit_celsius ? 'C' : 'F';
    snprintf(line, sizeof line, "%-10s %3d%c", "Current",
             ui_int(ui_display_temperature(c->wort_fahrenheit)), unit);
    pico_display_text_at_cell(0, 3, line);
    snprintf(line, sizeof line, "%-10s %3d%c", "Target",
             ui_int(ui_display_temperature(c->wort_target_fahrenheit)), unit);
    pico_display_text_at_cell(1, 3, line);
}
