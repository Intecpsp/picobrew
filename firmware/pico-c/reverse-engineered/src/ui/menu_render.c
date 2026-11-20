/* C renders a single selected menu item in the lower16-pixel strip. */
#include "ui_internal.h"

static void label(const char *text)
{
    PicoDisplay *d = &pico_ui_active->display;
    pico_graphics_fill_rectangle(d, 0, 16, 128, 16, 0);
    pico_graphics_fill_rectangle(d, 8, 16, 104, 15, 1);
    pico_graphics_bitmap(d, 0, 16, pico_c_menu_left_cap, 16, 15, 1);
    pico_graphics_text_color(d, 0);
    pico_display_builtin_font();
    pico_display_center_text_at_pixels((short)(24 - pico_display_cell_height() / 2), text);
    pico_display_custom_font();
    pico_graphics_text_color(d, 1);
}

void pico_ui_draw_menu_items_direction(uint32_t count, int selected, int first,
                                      uint32_t first_visible, int direction)
{
    (void)first_visible; /* The original C draw entry receives but ignores it. */
    ui_log("I", "MEN", "drawItems");
    int index = selected - first;
    if (index < 0)
        index = 0;
    /* Explicit bound for a malformed caller; normal OEM menu range is0..21. */
    if (index >= PICO_MENU_CAPACITY)
        index = PICO_MENU_CAPACITY - 1;
    label(pico_ui_active->menu_labels[index]);
    PicoDisplay *d = &pico_ui_active->display;
    if (direction) {
        pico_graphics_fill_rectangle(d, 112, 16, 16, 15, 0);
        pico_graphics_bitmap(d, 112, 16, direction < 0 ? pico_c_menu_down : pico_c_menu_up, 16, 15, 1);
        pico_display_flush();
        ui_delay(50);
    }
    pico_graphics_fill_rectangle(d, 112, 16, 16, 15, 0);
    pico_graphics_bitmap(d, 112, 16, count == 1 ? pico_c_menu_single : pico_c_menu_multiple, 16, 15, 1);
    pico_display_flush();
}

void pico_ui_draw_menu_items(uint32_t count, int selected, int first, uint32_t first_visible)
{
    pico_ui_draw_menu_items_direction(count, selected, first, first_visible, 0);
}

void pico_ui_draw_footer_option(const char *text, int more)
{
    label(text);
    pico_graphics_bitmap(&pico_ui_active->display, 112, 16,
                         more ? pico_c_menu_multiple : pico_c_menu_single, 16, 15, 1);
}

void pico_ui_draw_footer_single(const char *text)
{
    pico_ui_draw_footer_option(text, 0);
}
