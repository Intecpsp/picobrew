/* Original C instruction presentation: one sentence in the scrolling row,
 * with fixed-width labels for draining and the compact time/temperature line.
 * Data is extracted reproducibly by tools/extract_ui_assets.py.
 */
#include "ui_internal.h"

void pico_screen_get_instruction_lines(PicoTextScreen *screen, uint32_t screen_id,
                                       uint32_t language)
{
    if (screen_id >= 25) {
        ui_log("E", "SCREEN", "getScreenIdLines() invalid ScreenId arg");
        return;
    }
    if (language >= 3) {
        ui_log("E", "SCREEN", "getScreenIdLines() invalid language arg");
        language = 0;
    }
    const char *line = pico_c_instruction_text[screen_id][language];
    if (line)
        pico_screen_add_lines(screen, 1, line);
}

const char *pico_title_label(uint32_t title, uint32_t language)
{
    /* The original inclusive enum-count comparison reads past the five rows
     * at title5. Keep the same explicit bounds repair as the S/Pro source. */
    ui_assert(title < 5, "0 <= titleId && titleId <= TitleId::_EnumCount");
    ui_assert(language < 3, "0 <= language && language <= 2");
    return title >= 5 ? "<titleId>" : pico_ui_titles[title][language < 3 ? language : 0];
}

bool pico_title_id_is_valid(uint32_t title)
{
    return title < 5;
}

const char *pico_brew_display_state_label(uint32_t state, uint32_t language)
{
    ui_assert(state < 4, "isValid(brewStateId)");
    ui_assert(language < 3, "0 <= language && language <= 2");
    return state >= 4 ? "<brewState>" : pico_ui_brew_labels[state][language < 3 ? language : 0];
}

void pico_ui_draw_instruction_screen(uint32_t screen_id, uint32_t language, const char *title,
                                     int use_context_title)
{
    (void)use_context_title; /* Both C wrapper entries share the same layout. */
    char log[128], text[156];
    snprintf(log, sizeof log, "drawScreen %d %20s %s", (int)language, title,
             screen_id < 25 ? pico_ui_instruction_names[screen_id] : "<screenId>");
    ui_log("I", "LANG", log);
    if (screen_id == 19) {
        const PicoProgramContext *program = &pico_ui_active->app->session;
        snprintf(text, sizeof text, "%3d%c %02d:%02d",
                 ui_int(ui_display_temperature((double)(int32_t)program->temperature)),
                 pico_ui_active->network->settings->temperature_unit_celsius ? 'C' : 'F',
                 (int)((int32_t)program->duration / 60), (int)((int32_t)program->duration % 60));
        pico_display_fixed_width_center(0, text);
        return;
    }
    PicoTextScreen screen = {0};
    pico_screen_get_instruction_lines(&screen, screen_id, language);
    if (screen_id == 17 || screen_id == 18 || screen_id == 23) {
        if (screen.line_count == 1)
            pico_display_fixed_width_center(0, screen.lines[0]);
        return;
    }
    /* Bound the original strcpy/strcat assembly explicitly at the recovered
     * stack-buffer size. Valid OEM strings fit; remote titles cannot overflow. */
    size_t used = (size_t)snprintf(text, sizeof text, "%s%s", title, screen.line_count ? ":" : "");
    for (unsigned i = 0; i < screen.line_count && used < sizeof text; i++)
        used += (size_t)snprintf(text + used, sizeof text - used, " %s", screen.lines[i]);
    pico_display_start_scrolling(text, NULL);
}

void pico_ui_draw_instruction_with_header(uint32_t screen_id, uint32_t language, const char *title)
{
    pico_ui_draw_instruction_screen(screen_id, language, title, 0);
}

void pico_ui_draw_instruction_with_context(uint32_t screen_id, uint32_t language, const char *title)
{
    pico_ui_draw_instruction_screen(screen_id, language, title, 1);
}
