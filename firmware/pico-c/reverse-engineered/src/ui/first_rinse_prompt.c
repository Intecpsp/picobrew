/* OEM C0eb94 and its callback0x20004c50. */
#include "ui_internal.h"

static void first_rinse_heading(void)
{
    pico_display_start_scrolling("Pico needs a First Rinse. Run First Rinse now?", NULL);
}

void pico_ui_prompt_first_rinse(void)
{
    if (pico_ui_confirm_screen(first_rinse_heading)) {
        pico_display_stop_scrolling();
        pico_ui_active->services->run_first_rinse(pico_ui_active->service_context);
    }
    pico_display_stop_scrolling();
}
