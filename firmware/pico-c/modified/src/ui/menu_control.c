/* Pico C menu control adapted from the archived typed S/Pro routines.
 * Main C choose entry2000d8f0 adds scrolling and single-item direction rendering.
 */
#include "ui_internal.h"

uint32_t pico_menu_choose_with_timeout(uint32_t first_menu_row, uint32_t choice_count,
                                       uint32_t initial_index, int use_timeout,
                                       uint8_t default_index, uint8_t redraw, int timeout_seconds)
{
    bool still_fault;
    uint32_t selection_started_ms;
    int input_result;
    uint32_t now_ms;
    uint32_t encoder_position;
    uint32_t visible_row_capacity;
    uint32_t selected_row;
    uint32_t first_row;
    uint32_t first_visible_item;
    uint32_t last_still_poll_ms;
    uint32_t visible_item_end;
    int direction = 0;

    ui_assert((uint32_t)(choice_count != 0), "numOpts > 0");
    ui_assert((uint32_t)(initial_index < choice_count), "0 <= defOption && defOption < numOpts");
    if (choice_count <= initial_index) {
        initial_index = (choice_count - 1) & 0xff;
    }
    selection_started_ms = ui_millis();
    first_row = first_menu_row & 0xff;
    visible_row_capacity = 8 - first_menu_row;
    visible_item_end = visible_row_capacity;
    if ((int)choice_count < (int)visible_row_capacity) {
        visible_item_end = choice_count;
    }
    pico_encoder_set_range((PicoEncoder *)&pico_ui_active->encoder, first_menu_row,
                           first_menu_row + choice_count, (initial_index + first_row) & 0xff);
    if (redraw && pico_ui_active->app->carousel.pages[pico_ui_active->app->carousel.page_index]) {
        pico_display_clear();
        pico_ui_active->app->carousel.pages[pico_ui_active->app->carousel.page_index]();
    }
    pico_ui_draw_menu_items_direction(choice_count, (initial_index + first_row) & 255,
                                      first_row, 0, 0);
    input_result = pico_encoder_get_position((PicoEncoder *)&pico_ui_active->encoder);
    selected_row = (uint32_t)input_result & 0xff;
    pico_encoder_clear_click((PicoEncoder *)&pico_ui_active->encoder);
    first_visible_item = 0;
    last_still_poll_ms = selection_started_ms;
    while (true) {
        pico_local_poll();
#if PICO_STANDALONE
        /* A request can arrive while the C menu initializes and clears its
         * click latch. Dispatch the queued command from the application loop
         * even when that synthetic click was consumed by initialization. */
        if (pico_local_control_has_pending()) {
            pico_display_stop_scrolling();
            return UINT32_MAX;
        }
#endif
        if (pico_local_cancelled()) {
            pico_display_stop_scrolling();
            return UINT32_MAX;
        }
        pico_display_update_scrolling();
        if (((pico_ui_active->app->controller.phase != '\x04') &&
             (input_result = pico_filter_is_inserted(pico_ui_active->actuators),
              input_result == 0)) &&
            (pico_ui_active->app->controller.status < 6)) {
            pico_display_stop_scrolling();
            pico_ui_active->services->pause_until_resume(pico_ui_active->service_context);
            return (uint32_t)default_index;
        }
        if ((uint32_t)(pico_ui_active->app->controller.status - 9) < 2) {
            now_ms = ui_millis();
            if (2000 < now_ms - last_still_poll_ms) {
                still_fault = pico_still_blocked(pico_ui_active->network);
                if (still_fault) {
                    return 0;
                }
                last_still_poll_ms = now_ms;
            }
        }
        encoder_position = pico_encoder_get_position((PicoEncoder *)&pico_ui_active->encoder);
        encoder_position = encoder_position & 0xff;
        if ((use_timeout != 0) && (now_ms = ui_millis(), (uint32_t)(timeout_seconds * 1000) <
                                                             now_ms - selection_started_ms))
            break;
        if (encoder_position == selected_row) {
            selected_row = encoder_position;
            if ((redraw != '\0') &&
                (pico_ui_active->app->carousel.pages[pico_ui_active->app->carousel.page_index] !=
                 0)) {
                pico_display_clear();
                pico_ui_active->app->carousel.pages[pico_ui_active->app->carousel.page_index]();
                direction = 0;
                pico_ui_draw_menu_items_direction(choice_count, encoder_position, first_row,
                                                  first_visible_item & 0xff, 0);
                pico_display_flush();
            }
        } else {
            if (selected_row < encoder_position) {
                selected_row = (selected_row + 1) & 0xff;
                if ((int)(first_menu_row + choice_count) <= (int)selected_row) {
                    selected_row = (choice_count + ((first_row - 1) & 0xff)) & 0xff;
                    direction = 0;
                } else {
                    direction = -1;
                }
            } else if (selected_row == first_menu_row) {
                if ((int)selected_row < (int)(selected_row - 1)) {
                    selected_row = first_row;
                    direction = 0;
                }
            } else {
                selected_row = (selected_row - 1) & 0xff;
                direction = 1;
            }
            pico_encoder_set_position((PicoEncoder *)&pico_ui_active->encoder, selected_row);
            encoder_position = selected_row - first_menu_row;
            first_visible_item = encoder_position + 1;
            if ((int)(encoder_position + 1) < (int)visible_item_end) {
                first_visible_item = visible_item_end;
            }
            if ((int)choice_count < (int)first_visible_item) {
                first_visible_item = choice_count;
            }
            first_visible_item = first_visible_item + (first_menu_row - 8);
            first_visible_item = first_visible_item & ~((int)first_visible_item >> 0x1f);
            if ((int)encoder_position <= (int)first_visible_item) {
                first_visible_item = encoder_position;
            }
            visible_item_end = visible_row_capacity + first_visible_item;
            if ((int)choice_count <= (int)(visible_row_capacity + first_visible_item)) {
                visible_item_end = choice_count;
            }
            if ((redraw != '\0') &&
                (pico_ui_active->app->carousel.pages[pico_ui_active->app->carousel.page_index] !=
                 0)) {
                pico_display_clear();
                pico_ui_active->app->carousel.pages[pico_ui_active->app->carousel.page_index]();
            }
            pico_ui_draw_menu_items_direction(choice_count, selected_row, first_row,
                                              first_visible_item & 0xff, direction);
            pico_encoder_set_position((PicoEncoder *)&pico_ui_active->encoder, selected_row);
        }
        input_result = pico_encoder_take_click((PicoEncoder *)&pico_ui_active->encoder);
        if (input_result != 0) {
            input_result = pico_encoder_get_position((PicoEncoder *)&pico_ui_active->encoder);
            return (input_result - first_row) & 0xff;
        }
    }
    return (uint32_t)default_index;
}

uint32_t pico_menu_choose_default_timeout(uint32_t first_menu_row, uint32_t choice_count,
                                          uint32_t initial_index, int use_timeout,
                                          uint8_t default_index, uint8_t redraw)
{
    uint32_t menu_choose_with_timeout_result;

    menu_choose_with_timeout_result = pico_menu_choose_with_timeout(
        first_menu_row, choice_count, initial_index, use_timeout, default_index, redraw, 0x3c);
    return menu_choose_with_timeout_result;
}

uint32_t pico_menu_choose(uint32_t first_menu_row, uint32_t choice_count, uint32_t initial_index)
{
    uint32_t menu_choose_default_timeout_result;

    menu_choose_default_timeout_result = pico_menu_choose_default_timeout(
        first_menu_row, choice_count, initial_index, 0, '\0', '\0');
    return menu_choose_default_timeout_result;
}

uint32_t pico_menu_select_action(uint32_t first_menu_row, int initial_index)
{
    uint32_t menu_choose_result;

    ui_assert((uint32_t)(0 < (int)pico_ui_active->app->menu.count),
              "pico_ui_active->app->menu.getCount() > 0");
    if (initial_index < 0) {
        menu_choose_result = 0;
    } else {
        menu_choose_result = (uint32_t)(initial_index < (int)pico_ui_active->app->menu.count);
    }
    ui_assert(menu_choose_result,
              "0 <= selected && selected < pico_ui_active->app->menu.getCount()");
    if ((int)pico_ui_active->app->menu.count <= initial_index) {
        initial_index = pico_ui_active->app->menu.count - 1;
    }
    menu_choose_result = pico_menu_choose(first_menu_row, pico_ui_active->app->menu.count & 0xff,
                                          initial_index & ~(initial_index >> 0x1f) & 0xff);
    if (pico_local_cancelled() || menu_choose_result == UINT32_MAX)
        return UINT32_MAX;
    menu_choose_result =
        pico_menu_action_at((PicoMenu *)&pico_ui_active->app->menu, menu_choose_result);
    return menu_choose_result;
}

bool pico_ui_confirm_at_row(uint32_t menu_row)
{
    uint32_t menu_choose_result;

    pico_label_no(pico_ui_active->menu_labels[0]);
    pico_label_yes(pico_ui_active->menu_labels[1]);
    menu_choose_result = pico_menu_choose(menu_row, 2, 0);
    return menu_choose_result == 1;
}

