/* Pico C0x20009a84 and C0x20006de0. */
#include "ui_internal.h"

void pico_ui_show_scrolling_message(const char *text, void (*redraw)(void))
{
    ui_assert(redraw != NULL, "function != NULL");
    pico_display_start_scrolling(text, redraw);
    pico_display_wait_for_click();
    pico_display_stop_scrolling();
}

void pico_ui_draw_continue_hint(void)
{
    pico_display_center_text(3, "<Continue>");
}
