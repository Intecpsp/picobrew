/* Pico C UI workflow reconstruction, using the archived typed S/Pro interfaces.
 * C entry correspondence and validation are recorded separately; inherited
 * control flow is an implementation candidate until the C comparisons pass. */
#include "ui_internal.h"
#include "workflow_text.inc"

static unsigned workflow_language(void)
{
    unsigned language = pico_ui_active->network->settings->language;
    return language == 1 ? 1 : language == 2 ? 2 : 0;
}

void pico_ui_wait_for_resume(void)
{
    int encoder_take_click_result;

    do {
        if (pico_local_cancelled())
            return;
        while (encoder_take_click_result =
                   pico_encoder_take_click((PicoEncoder *)&pico_ui_active->encoder),
               encoder_take_click_result == 0) {
            if (pico_local_cancelled())
                return;
            pico_ui_draw_paused();
        }
        encoder_take_click_result = pico_filter_is_inserted(pico_ui_active->actuators);
    } while (!pico_local_cancelled() && encoder_take_click_result == 0);
    return;
}

void pico_ui_wait_for_footer_press(const char *text)
{
    int encoder_take_click_result;

    pico_ui_draw_footer_single(text);
    pico_display_flush();
    pico_encoder_clear_click((PicoEncoder *)&pico_ui_active->encoder);
    do {
        encoder_take_click_result =
            pico_encoder_take_click((PicoEncoder *)&pico_ui_active->encoder);
    } while (!pico_local_cancelled() && encoder_take_click_result == 0);
    return;
}

/* Main 0x20009dd0: two sequential C hose/filter prompts. */
void pico_ui_prompt_deep_clean_setup(void)
{
    pico_ui_show_scrolling_message(wf_clean_setup[0], pico_ui_draw_continue_footer);
    pico_display_clear();
    pico_ui_show_scrolling_message(wf_clean_setup[1], pico_ui_draw_start_clean_footer);
}

void pico_ui_wait_for_filter_insertion(void)
{
    int step_filter_is_inserted_result;

    step_filter_is_inserted_result = pico_filter_is_inserted(pico_ui_active->actuators);
    if (step_filter_is_inserted_result == 0) {
        pico_display_clear_scrolling();
        pico_ui_draw_instruction_with_context(1,
                                              (uint32_t)pico_ui_active->network->settings->language,
                                              pico_ui_active->app->session.title);
        pico_display_flush();
        pico_ui_active->services->wait_for_filter(pico_ui_active->service_context);
    }
    pico_display_clear_scrolling();
    return;
}

void pico_ui_enter_wifi_password(void)
{
    pico_ui_draw_password_heading();
    pico_ui_active->network->settings->wifi_password[0] = 0;
    pico_encoder_edit_text(pico_ui_active->network->settings->wifi_password, 0x40);
    pico_display_clear();
    return;
}

uint32_t pico_ui_choose_language(void)
{
    uint32_t menu_populate_languages_result;

    menu_populate_languages_result = pico_menu_populate_languages();
    pico_display_clear();
    pico_ui_draw_language_menu();
    menu_populate_languages_result = pico_menu_choose(3, menu_populate_languages_result, 0);
    if (menu_populate_languages_result != 1) {
        if (menu_populate_languages_result == 0) {
            pico_ui_active->network->settings->language = 0;
            return 0;
        }
        if (menu_populate_languages_result != 2) {
            return menu_populate_languages_result;
        }
    }
    pico_ui_active->network->settings->language = (char)menu_populate_languages_result;
    return menu_populate_languages_result;
}

uint32_t pico_ui_choose_units(void)
{
    uint32_t ui_draw_units_menu_result;

    pico_display_clear();
    ui_draw_units_menu_result = pico_ui_draw_units_menu();
    ui_draw_units_menu_result = pico_menu_choose(3, ui_draw_units_menu_result, 0);
    pico_ui_active->network->settings->temperature_unit_celsius = ui_draw_units_menu_result != 0;
    return (uint32_t)(ui_draw_units_menu_result != 0);
}

uint32_t pico_ui_choose_program(void)
{
    uint32_t menu_populate_still_actions_result;

    pico_display_clear();
    pico_ui_draw_subheader("CHOOSE A PROGRAM");
    menu_populate_still_actions_result = pico_menu_populate_still_actions(2);
    menu_populate_still_actions_result = pico_menu_choose(4, menu_populate_still_actions_result, 0);
    return menu_populate_still_actions_result;
}

/* Main 0x20009b0c; callback literal 0x20009b50 selects the OK footer. */
void pico_ui_require_first_rinse(void)
{
    pico_display_clear();
    pico_ui_show_scrolling_message(wf_first_rinse_warning[workflow_language()],
                                   pico_ui_draw_ok_footer);
}

/* Main 0x2000dee4. These C prompts are not the S/Pro keg-wand sequence. */
int pico_ui_prompt_vacuum(int stage)
{
    char text[152];
    pico_display_clear();
    strcpy(text, pico_title_label(2, pico_ui_active->network->settings->language));
    switch (stage) {
    case 0:
        strcpy(text, wf_vacuum_setup[0]);
        strcpy(pico_ui_active->menu_labels[0], wf_vacuum_start[0]);
        pico_label_exit(pico_ui_active->menu_labels[1]);
        pico_display_start_scrolling(text, NULL);
        {
            uint32_t choice = pico_menu_choose(7, 2, 0);
            pico_display_stop_scrolling();
            return choice == 0;
        }
    case 1:
        strcpy(text, wf_vacuum_draining[0]);
        pico_display_center_text(0, text);
        pico_ui_draw_done_footer();
        pico_display_flush();
        pico_encoder_clear_click(&pico_ui_active->encoder);
        break;
    case 2:
        pico_display_center_text(0, wf_vacuum_flushing[0]);
        pico_display_flush();
        break;
    case 3:
        strcat(text, wf_vacuum_reservoir_plug[0]);
        pico_ui_show_scrolling_message(text, pico_ui_draw_stop_footer);
        break;
    }
    return 1;
}

uint32_t pico_ui_wait_for_continue(void)
{
    uint32_t menu_choose_result;

    pico_label_continue(pico_ui_active->menu_labels[0]);
    menu_choose_result = pico_menu_choose(7, 1, 0);
    return menu_choose_result;
}

void pico_prompt_insert_filter_with_alarm(void)
{
    ui_millis();
    pico_ui_active->services->buzzer(pico_ui_active->service_context, true);
    pico_screen_reinsert_step_filter();
    pico_display_flush();
    pico_ui_active->services->alarm_filter_missing(pico_ui_active->service_context);
    pico_display_clear();
    return;
}

uint32_t pico_run_drain_instructions(uint32_t second_screen_id)
{
    int drain_running;
    int selected_action;
    uint32_t result_action;
    uint32_t drain_menu_id;
    uint32_t screen_ids[2];

    screen_ids[0] = 14;
    screen_ids[1] = second_screen_id;
    selected_action =
        pico_ui_run_instruction_sequence(screen_ids, 2, 0, pico_ui_active->app->session.program_id,
                                         pico_ui_active->app->session.title, '\x01');
    if (selected_action == 0x17) {
        drain_running = pico_actuator_drain_is_on(pico_ui_active->actuators);
        if (!drain_running) {
            pico_actuator_drain_on(pico_ui_active->actuators);
        }
        while (true) {
            if (pico_local_cancelled())
                break;
            drain_running = pico_actuator_drain_is_on(pico_ui_active->actuators);
            if (drain_running) {
                drain_menu_id = 0x11;
            } else {
                drain_menu_id = 0x12;
            }
            drain_menu_id = pico_ui_active->services->execute_instruction(
                pico_ui_active->service_context, drain_menu_id, false);
            if (1 < drain_menu_id - 0x1f)
                break;
            drain_running = ui_read(8);
            selected_action = 1 - (uint32_t)drain_running;
            if (1 < drain_running) {
                selected_action = 0;
            }
            ui_write(8, selected_action);
        }
        pico_actuator_drain_off(pico_ui_active->actuators);
        result_action = 0x17;
    } else {
        ui_log("W", "RCTL", "Drain setup aborted by user");
        result_action = 0x22;
    }
    return result_action;
}

/* Main 0x2000e3f0. Every C heading is at row zero. */
void pico_ui_manual_network_step(int stage)
{
    unsigned language = workflow_language();
    pico_display_clear();
    if (stage == 0) {
        pico_display_center_text(0, wf_network_prompt[language]);
        pico_ui_wait_for_continue();
    } else if (stage == 1) {
        pico_display_center_text(0, wf_network_name[language]);
    } else if (stage == 2) {
        pico_display_center_text(0, wf_network_security[language]);
        for (unsigned i = 0; i < 3; ++i)
            strcpy(pico_ui_active->menu_labels[i], wf_network_options[i]);
    }
}

/* Main 0x20009b54. */
void pico_ui_prompt_picopak_association(void)
{
    pico_display_clear();
    pico_ui_show_scrolling_message(wf_picopak_association[workflow_language()],
                                   pico_ui_draw_continue_footer);
}

/* Main 0x20009e10. */
void pico_ui_show_rinse_finished(void)
{
    char text[156];
    snprintf(text, sizeof text, "%s: ", pico_ui_active->app->recipe.program->title);
    strcat(text, wf_clean_finish[0]);
    pico_ui_show_scrolling_message(text, pico_ui_draw_continue_footer);
}

/* Main 0x20009bc4. Stage numbers come from the C first-rinse control flow. */
void pico_ui_first_rinse_step(uint32_t stage)
{
    char text[152];
    unsigned language = workflow_language();
    pico_display_clear();
    pico_label_first_rinse(text);
    switch (stage) {
    case 0:
        strcat(text, wf_rinse_setup[language]);
        pico_ui_show_scrolling_message(text, pico_ui_draw_continue_footer);
        return;
    case 1:
        pico_display_center_text(0, text);
        pico_label_insert_step_filter(text);
        pico_display_center_text(1, text);
        break;
    case 2: {
        /* The French OEM branch retains rows 3/4, including their clipping. */
        short row = language == 2 ? 3 : 0;
        pico_display_center_text(row, wf_rinse_prime_steam[language * 2]);
        pico_display_center_text(row + 1, wf_rinse_prime_steam[language * 2 + 1]);
        break;
    }
    case 3:
        if (language == 0) {
            pico_display_center_text(0, text);
            pico_display_center_text(1, wf_rinse_prime_pump[0]);
        } else {
            pico_display_center_text(0, wf_rinse_prime_pump[language * 2 - 1]);
            pico_display_center_text(1, wf_rinse_prime_pump[language * 2]);
        }
        break;
    case 4:
        pico_display_center_text(0, wf_rinse_steam_flush[language * 2]);
        pico_display_center_text(1, wf_rinse_steam_flush[language * 2 + 1]);
        break;
    case 5:
        pico_display_center_text(0, wf_rinse_flush[language]);
        break;
    case 6:
        pico_display_center_text(0, wf_rinse_pump_error[language]);
        break;
    case 7:
        pico_display_center_text(0, wf_rinse_heat_error[language]);
        break;
    case 8:
        pico_display_start_scrolling(wf_rinse_finish[0], NULL);
        return;
    case 9:
        pico_screen_help_address();
        return;
    case 10:
        strcat(text, wf_rinse_hose_setup[0]);
        pico_ui_show_scrolling_message(text, pico_ui_draw_start_rinse_footer);
        return;
    default:
        return;
    }
    pico_display_flush();
}

void pico_ui_wait_for_stop(void)
{
    pico_label_done(pico_ui_active->menu_labels[0]);
    pico_menu_choose(7, 1, 0);
    return;
}

/* Main 0x2000b7f4: reporting order and footer callbacks are observable. */
void pico_ui_rack_beer_step(uint32_t stage)
{
    char text[148];
    unsigned language = workflow_language();
    void (*footer)(void);
    pico_display_clear();
    pico_label_rack_beer(text);
    switch (stage) {
    case 0:
        strcat(text, wf_rack_waste_hose[0]);
        footer = pico_ui_draw_start_flush_footer;
        break;
    case 1:
        strcat(text, wf_rack_flush[language]);
        pico_ui_active->app->device_state = 5;
        pico_api_report_state(pico_ui_active->network);
        footer = pico_ui_draw_stop_footer;
        break;
    case 2:
        strcat(text, wf_rack_remove_seal[0]);
        pico_ui_active->app->device_state = 5;
        pico_api_report_state(pico_ui_active->network);
        footer = pico_ui_draw_continue_footer;
        break;
    case 3:
        strcat(text, wf_rack_connect_hose[language]);
        footer = pico_ui_draw_continue_footer;
        break;
    case 5:
        strcat(text, wf_rack_tube[language]);
        footer = pico_ui_draw_start_flush_footer;
        break;
    case 6:
        strcat(text, wf_rack_trub[language]);
        footer = pico_ui_draw_stop_footer;
        break;
    case 7:
        strcat(text, wf_rack_transfer[language]);
        footer = pico_ui_draw_start_transfer_footer;
        break;
    case 8:
        strcat(text, wf_rack_done[language]);
        pico_ui_active->app->device_state = 2;
        pico_api_report_state(pico_ui_active->network);
        footer = pico_ui_draw_stop_footer;
        break;
    default:
        return;
    }
    pico_ui_show_scrolling_message(text, footer);
}

/* Main 0x2000eaa4; stages 2/3 intentionally do not flush in the OEM code. */
bool pico_ui_descale_step(uint32_t stage)
{
    char text[252];
    pico_display_clear();
    pico_label_descale(text);
    switch (stage) {
    case 0:
        strcat(text, wf_descale_setup[0]);
        pico_ui_active->app->device_state = 9;
        pico_api_report_state(pico_ui_active->network);
        pico_ui_show_scrolling_message(text, pico_ui_draw_continue_footer);
        break;
    case 1:
        pico_display_center_text(0, wf_descale_headings[0]);
        pico_display_center_text(1, wf_descale_headings[1]);
        pico_display_flush();
        break;
    case 2:
        pico_display_center_text(0, wf_descale_headings[0]);
        pico_display_center_text(1, wf_descale_headings[2]);
        break;
    case 3:
        pico_display_center_text(0, wf_descale_headings[0]);
        pico_ui_draw_footer_single(wf_descale_headings[3]);
        pico_encoder_clear_click(&pico_ui_active->encoder);
        break;
    case 4:
        pico_display_center_text(0, wf_descale_headings[3]);
        return pico_ui_confirm_at_row(1);
    case 5:
        strcat(text, wf_descale_refill[0]);
        pico_ui_show_scrolling_message(text, pico_ui_draw_ok_footer);
        break;
    case 6:
        pico_display_center_text(1, wf_descale_setting_up[0]);
        pico_display_flush();
        break;
    case 7:
        strcat(text, wf_descale_hoses[0]);
        pico_ui_show_scrolling_message(text, pico_ui_draw_continue_footer);
        break;
    }
    return true;
}

/* Main 0x2000dcd4 / helper 0x200083bc. OEM translations retained verbatim. */
int pico_ui_confirm_continue_or_abort(void)
{
    pico_ui_draw_current_program_heading();
    return pico_menu_choose(3, 2, 0) == 0;
}

void pico_ui_enter_manual_network(void)
{
    uint32_t menu_choose_result;

    pico_ui_manual_network_step(0);
    pico_ui_active->network->settings->wifi_ssid[0] = 0;
    pico_ui_manual_network_step(1);
    pico_encoder_edit_text(pico_ui_active->network->settings->wifi_ssid, 0x21);
    pico_ui_manual_network_step(2);
    menu_choose_result = pico_menu_choose(3, 3, 0);
    if (menu_choose_result == 1) {
        pico_ui_active->network->settings->wifi_security = '\x05';
    } else if (menu_choose_result == 2) {
        pico_ui_active->network->settings->wifi_security = '\a';
    } else {
        pico_ui_active->network->settings->wifi_security = '\x02';
    }
    pico_display_clear();
    pico_ui_active->network->settings->wifi_password[0] = 0;
    if (pico_ui_active->network->settings->wifi_security != '\a') {
        pico_ui_enter_wifi_password();
    }
    pico_display_clear();
    return;
}

/* Main 0x200105c4. */
void pico_ui_start_descale(void)
{
    if (pico_ui_confirm_screen(pico_ui_draw_descale_preparation)) {
        pico_display_stop_scrolling();
        pico_ui_active->services->run_descale(pico_ui_active->service_context);
        return;
    }
    pico_display_clear();
    pico_ui_draw_descale_rinse_preparation();
    pico_ui_wait_for_continue();
    pico_display_stop_scrolling();
}

uint32_t pico_ui_run_instruction_screen(uint32_t screen_id, int sound_alarm, int program_id,
                                        const char *title, uint8_t use_context_title)
{
    uint32_t menu_select_action_result;
    PicoMenu instruction_menu;

    pico_display_clear_scrolling();
    pico_ui_draw_instruction_screen(screen_id,
                                    (uint32_t)pico_ui_active->network->settings->language, title,
                                    (uint32_t)use_context_title);
    if (sound_alarm != 0) {
        pico_alarm_until_press_or_timeout(pico_ui_active->network->settings->language,
                                          pico_ui_active->network->settings->detailed_status,
                                          60000);
    }
    pico_menu_initialize(&instruction_menu);
    switch (screen_id) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
        pico_menu_add_action(&instruction_menu, 0x17);
        if (program_id == 4) {
            menu_select_action_result = 0x19;
            goto add_stage_action;
        }
        break;
    default:
        ui_log("E", "SCR", "Unsuported screen");
        break;
    case 9:
    case 0xc:
    case 0xd:
    case 0xe:
    case 0xf:
    case 0x10:
    case 0x15:
    case 0x16:
        menu_select_action_result = 0x17;
        goto add_stage_action;
    case 10:
        menu_select_action_result = 0x1a;
    add_stage_action:
        pico_menu_add_action(&instruction_menu, menu_select_action_result);
        break;
    case 0xb:
        pico_menu_add_action(&instruction_menu, 0x1d);
        if (program_id == 3) {
            menu_select_action_result = 0x1c;
            goto add_stage_action;
        }
        break;
    case 0x11:
        menu_select_action_result = 0x1f;
        goto add_rinse_actions;
    case 0x12:
        menu_select_action_result = 0x20;
    add_rinse_actions:
        pico_menu_add_action(&instruction_menu, menu_select_action_result);
        menu_select_action_result = 0x1e;
        goto add_final_action;
    case 0x18:
        menu_select_action_result = 0x18;
        goto add_final_action;
    }
    menu_select_action_result = 0x22;
add_final_action:
    pico_menu_add_action(&instruction_menu, menu_select_action_result);
    pico_menu_populate_from_menu(&instruction_menu,
                                 (uint32_t)pico_ui_active->network->settings->language,
                                 (uint32_t)pico_ui_active->network->settings->detailed_status);
    menu_select_action_result = pico_menu_select_action(7, 0);
    return menu_select_action_result;
}

int pico_ui_run_instruction_sequence(const uint32_t *screen_ids, int count, int sound_first_alarm,
                                     int program_id, const char *title, uint8_t use_context_title)
{
    ui_assert(screen_ids != NULL && count > 0, "screenIds != NULL && screenIdsCount > 0");
    if (screen_ids == NULL)
        return 0;
    uint32_t action = 0;
    for (int index = 0; index < count; ++index) {
        bool sound_alarm = sound_first_alarm != 0 && index == 0;
        action = pico_ui_run_instruction_screen(screen_ids[index], sound_alarm, program_id, title,
                                                use_context_title);
        if (action != 0x17)
            return (int)action;
    }
    return (int)action;
}

uint32_t pico_get_temperature_input(uint32_t action_id, const char *program_title,
                                    uint32_t fahrenheit)
{
    const char *action_label;
    int button_pressed;

    char footer_text[24];
    char log_message[128];

    action_label =
        pico_action_label(action_id, (uint32_t)pico_ui_active->network->settings->language,
                          (uint32_t)pico_ui_active->network->settings->detailed_status);
    ui_cstring_copy(footer_text, 0x16, action_label);
    pico_encoder_set_step((PicoEncoder *)&pico_ui_active->encoder, 1);
    pico_encoder_set_range((PicoEncoder *)&pico_ui_active->encoder, 0x3c, 0xcb, fahrenheit);
    while (button_pressed = pico_encoder_take_click((PicoEncoder *)&pico_ui_active->encoder),
           button_pressed == 0) {
        if (pico_local_cancelled())
            return fahrenheit;
        fahrenheit = pico_encoder_get_position((PicoEncoder *)&pico_ui_active->encoder);
        pico_display_clear_scrolling();
        pico_display_bitmap_at(5, 0, &pico_ui_asset_temperature_header);
        pico_display_bitmap_at(104, 0, &pico_ui_asset_temperature_footer);
        pico_display_large_temperature_at_row((double)fahrenheit, 0, 0);
        pico_ui_draw_footer_single(footer_text);
        pico_display_flush();
    }
    snprintf(log_message, 0x80, "%s \"%s\" %dF", "getTemperatureInput", program_title,
             (int)fahrenheit);
    ui_log("I", "UI", log_message);
    return fahrenheit;
}

int pico_get_time_input(uint32_t action_id, const char *program_title, int minutes)
{
    const char *action_label;
    int button_or_increment;
    char footer_text[24];
    char log_message[128];

    action_label =
        pico_action_label(action_id, (uint32_t)pico_ui_active->network->settings->language,
                          (uint32_t)pico_ui_active->network->settings->detailed_status);
    ui_cstring_copy(footer_text, 0x16, action_label);
    if (minutes < 0x3c) {
        button_or_increment = 1;
    } else {
        button_or_increment = 5;
    }
    pico_encoder_set_step((PicoEncoder *)&pico_ui_active->encoder, button_or_increment);
    pico_encoder_set_range((PicoEncoder *)&pico_ui_active->encoder, 0, 0x253, minutes);
    while (button_or_increment = pico_encoder_take_click((PicoEncoder *)&pico_ui_active->encoder),
           button_or_increment == 0) {
        if (pico_local_cancelled()) {
            pico_encoder_set_step(&pico_ui_active->encoder, 1);
            return minutes;
        }
        minutes = pico_encoder_get_position((PicoEncoder *)&pico_ui_active->encoder);
        if (minutes < 0x3c) {
            button_or_increment = 1;
        } else {
            button_or_increment = 5;
        }
        pico_encoder_set_step((PicoEncoder *)&pico_ui_active->encoder, button_or_increment);
        pico_display_clear_scrolling();
        pico_display_large_timer_at_row(minutes, 0, 0, 0);
        pico_display_bitmap_at(5, 0, &pico_ui_asset_temperature_header);
        pico_display_bitmap_at(104, 0, &pico_ui_asset_temperature_footer);
        pico_ui_draw_footer_single(footer_text);
        pico_display_flush();
    }
    pico_encoder_set_step((PicoEncoder *)&pico_ui_active->encoder, 1);
    snprintf(log_message, 0x80, "%s \"%s\" %d min", "getTimeInput", program_title, minutes);
    ui_log("I", "UI", log_message);
    return minutes;
}
