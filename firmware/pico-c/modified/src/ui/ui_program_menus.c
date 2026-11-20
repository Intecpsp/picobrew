/* C service/program menus. Original entries: binary setup 0x2000dc90,
 * service 0x2000f1c0, utilities 0x20011758. The utility table is present at
 * main 0x20022864, with the same values in the SSD1309 build. */
#include "ui_internal.h"

#if PICO_STANDALONE
static const uint32_t ui_utilities_actions[] = {40, 41, 42, 43, 44, 45, 46, 48, 39};
#else
static const uint32_t ui_utilities_actions[] = {40, 41, 42, 43, 44, 45, 46, 48, 47, 39};
#endif

bool pico_ui_choose_binary_option(const char *title)
{
    if (pico_ui_active->app->session.session_type != 1) {
        uint32_t count = pico_screen_fill_reservoir_and_start(title);
        uint32_t choice = pico_menu_choose(7, count, 0);
        if (pico_local_cancelled() || choice != 0) {
            pico_display_stop_scrolling();
            return false;
        }
        if (pico_ui_active->app->session.session_type == 0) {
            pico_display_stop_scrolling();
            pico_display_clear();
            /* Main 0x20009b9c / literal 0x20009bbc. */
            pico_ui_show_scrolling_message(
                "Place cozy on keg and connect IN & OUT hoses securely to keg lid",
                pico_ui_draw_continue_footer);
        }
        pico_display_stop_scrolling();
    }
    return true;
}

void pico_ui_service_menu(void)
{
    uint32_t menu_populate_service_result;

    do {
        pico_display_clear();
        menu_populate_service_result = pico_menu_populate_service();
        char service_heading[24];
        pico_label_service(service_heading);
        pico_ui_draw_subheader(service_heading);
        menu_populate_service_result = pico_menu_choose(3, menu_populate_service_result, 0);
        switch (menu_populate_service_result) {
        case 0:
            pico_ui_network_menu();
            break;
        case 1:
            pico_ui_active->services->drain_reservoir(pico_ui_active->service_context);
            break;
        case 2:
            pico_ui_active->services->drain_step_filter(pico_ui_active->service_context);
            break;
        case 3:
            pico_ui_active->services->run_circulation(pico_ui_active->service_context);
            break;
        case 4:
            pico_diagnostics_menu();
            break;
        case 5:
#if PICO_STANDALONE
            return; /* The legacy factory-reset path depends on cloud firmware. */
#else
            bool ui_confirm_at_row_result;
            int mcu_image1_exists_result;
            pico_display_clear();
            pico_ui_draw_firmware_reset_question();
            ui_confirm_at_row_result = pico_ui_confirm_at_row(1);
            if (ui_confirm_at_row_result) {
                pico_display_stop_scrolling();
                pico_display_clear();
                pico_ui_draw_clear_wifi_question();
                ui_confirm_at_row_result = pico_ui_confirm_at_row(1);
                if (ui_confirm_at_row_result) {
                    pico_storage_delete_wifi(pico_ui_active->network);
                }
                pico_display_stop_scrolling();
                pico_display_clear();
                pico_screen_downloading_firmware();
                pico_display_flush();
                mcu_image1_exists_result =
                    pico_storage_recovery_image_exists(pico_ui_active->network);
                if (mcu_image1_exists_result == 0) {
                    pico_display_clear();
                    /* Main literal 0x2000f27c, callback 0x2000b2cc. */
                    pico_ui_show_scrolling_message("No Default Image please contact support",
                                                   pico_ui_draw_continue_footer);
                } else {
                    pico_storage_delete_user_files(pico_ui_active->network);
                    ui_delay(1000);
                    pico_ui_active->network->platform->reset_mcu(
                        pico_ui_active->network->platform_context);
                }
            }
            pico_display_stop_scrolling();
            break;
#endif
        default:
            return;
        }
    } while (!pico_local_cancelled());
}

void pico_utilities_menu(void)
{
    uint32_t menu_select_action_result;

    do {
        pico_display_clear_scrolling();
        pico_ui_draw_utilities_heading();
        pico_menu_populate_action_labels(
            ui_utilities_actions, sizeof ui_utilities_actions / sizeof ui_utilities_actions[0],
            (uint32_t)pico_ui_active->network->settings->language,
            (uint32_t)pico_ui_active->network->settings->detailed_status);
        menu_select_action_result = pico_menu_select_action(3, 0);
        switch (menu_select_action_result) {
        case 0x27:
            return;
        case 0x28:
            pico_ui_active->services->run_first_rinse(pico_ui_active->service_context);
            break;
        case 0x29:
            pico_ui_active->services->run_rinse(pico_ui_active->service_context);
            break;
        case 0x2a:
            pico_ui_active->services->run_circulation_program(pico_ui_active->service_context);
            break;
        case 0x2b:
            pico_ui_active->services->run_rack_beer(pico_ui_active->service_context);
            break;
        case 0x2c:
            pico_ui_active->services->drain_step_filter(pico_ui_active->service_context);
            break;
        case 0x2d:
            pico_ui_active->services->drain_reservoir(pico_ui_active->service_context);
            break;
        case 0x2e:
            pico_ui_active->services->run_descale(pico_ui_active->service_context);
            break;
        case 0x2f:
            pico_ui_active->services->run_still(pico_ui_active->service_context);
            break;
        case 0x30:
            pico_ui_active->services->run_circulation(pico_ui_active->service_context);
            break;
        default:
            ui_log("E", "MENU", "Unsupported action");
            return;
        }
    } while (!pico_local_cancelled());
}
