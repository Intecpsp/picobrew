/* Pico C still prompts and the typed network-to-display adapter. */
#include "ui_internal.h"

void pico_ui_confirm_still_cleaned(void)
{
    pico_display_clear();
    pico_ui_show_scrolling_message("Please confirm that you have cleaned your PicoStill prior to use.",
                                   pico_ui_draw_confirm_footer);
}

void pico_ui_show_still_communication_failure(void)
{
    pico_display_clear();
    pico_ui_show_scrolling_message("Connection lost. Please check PicoStill.",
                                   pico_ui_draw_continue_footer);
}

void pico_ui_show_still_completion(void)
{
    pico_ui_active->services->alarm_until_press(pico_ui_active->service_context);
    pico_display_clear();
    pico_display_center_text(0, "Release pressure");
    pico_ui_wait_for_continue();
}

void pico_ui_show_still_temperature_warning(void)
{
    pico_ui_active->services->alarm_until_press(pico_ui_active->service_context);
    pico_display_clear();
    pico_display_center_text(0, "Still T2 overtemp");
    pico_ui_wait_for_continue();
}

void pico_ui_still_fan_menu(void)
{
    uint32_t count = pico_menu_populate_still_fan();
    uint32_t selected = pico_menu_choose_default_timeout(7, count, 0, 1, (uint8_t)(count - 1), 1);
    if (selected < 2) {
        /* These rows are retained from C0x2000f570, even on its compact display. */
        pico_display_clear();
        pico_display_center_text(2, "Sending To Still");
        PicoControlSignals *s = &pico_ui_active->app->control_signals;
        s->progress_row = 3;
        s->progress_start_column = 5;
        s->progress_end_column = 16;
        s->progress_glyph_index = 0;
        s->progress_active = 1;
        pico_still_set_fan(pico_ui_active->network, selected != 0);
        pico_display_stop_progress_indicator(s);
    }
}

int pico_ui_choose_still(void *context, const char identifiers[][22], size_t count)
{
    PicoUi *previous = pico_ui_active;
    PicoUi *ui = context;
    pico_ui_bind(ui);
    if (count > PICO_MENU_CAPACITY)
        count = PICO_MENU_CAPACITY;
    for (size_t i = 0; i < count; i++)
        ui_copy(ui->menu_labels[i], PICO_MENU_LABEL_LENGTH, identifiers[i]);
    pico_display_clear();
    pico_ui_draw_subheader("CHOOSE A STILL");
    int selected = (int)pico_menu_choose(3, (uint32_t)count, 0);
    pico_ui_bind(previous);
    return selected;
}

void pico_ui_network_event(void *context, PicoNetworkUiEvent event, const char *text)
{
    PicoUi *previous = pico_ui_active;
    pico_ui_bind(context);
    switch (event) {
    case PICO_NET_UI_CLEAR:
        pico_display_clear();
        break;
    case PICO_NET_UI_DEVICE:
        pico_display_device_bitmap();
        break;
    case PICO_NET_UI_DEVICE_CONNECTED:
        pico_display_device_bitmap_variant_a();
        break;
    case PICO_NET_UI_DEVICE_FAILED:
        pico_display_device_bitmap_variant_b();
        break;
    case PICO_NET_UI_FLUSH:
        pico_display_flush();
        break;
    case PICO_NET_UI_CONNECTING:
        pico_display_clear();
        pico_ui_active->display.wrap = 0;
        pico_display_text_at_cell(0, 0, text);
        pico_display_clear_row(1);
        pico_display_text_at_cell(1, 0, "Connecting");
        pico_ui_active->display.wrap = 1;
        pico_display_flush();
        break;
    case PICO_NET_UI_STOP_PROGRESS:
        pico_display_stop_progress_indicator(&pico_ui_active->app->control_signals);
        break;
    case PICO_NET_UI_STOP_SCROLLING:
        pico_display_stop_scrolling();
        break;
    case PICO_NET_UI_REGISTRATION_CODE:
        pico_ui_draw_registration_code(text);
        break;
    case PICO_NET_UI_REFRESH_PAGE:
        pico_display_refresh_current_page();
        break;
    case PICO_NET_UI_UPDATING:
        pico_display_logo();
        pico_screen_updating_firmware();
        break;
    case PICO_NET_UI_DOWNLOADING:
        pico_display_clear_rows(4, 7);
        pico_screen_downloading_firmware();
        break;
    case PICO_NET_UI_UPDATE_FAILED:
        pico_screen_firmware_update_failed();
        break;
    case PICO_NET_UI_STILL_CONNECTING:
        pico_screen_still_connecting(text);
        break;
    case PICO_NET_UI_STILL_EXITING:
        pico_screen_still_exiting();
        break;
    case PICO_NET_UI_RESET_SHUTTLE:
        pico_display_clear();
        pico_ui_draw_header("RESET SHUTTLE PUMP");
        pico_display_center_text(1, "Removing Scaler");
        pico_display_flush();
        break;
    }
    pico_ui_bind(previous);
}
