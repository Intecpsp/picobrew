/* C diagnostic workflows recovered from their original UI and device calls. */
#include "ui_internal.h"
#include "pico_c/local_control.h"

void pico_report_error(int error_code, int allow_continue, int report_to_server)
{
    PicoUi *ui = pico_ui_active;
    pico_display_stop_progress_indicator(&ui->app->control_signals);
    pico_display_custom_font();
    if (!allow_continue) {
        pico_control_stop(ui->control);
        pico_actuator_steam_off(ui->actuators);
    }
    pico_display_clear();
    pico_ui_draw_error(error_code);
    pico_display_flush();
    if (report_to_server &&
        ui->network->platform->wifi_status(ui->network->platform_context) == 3 && error_code != 7)
        pico_api_error(ui->network, error_code);
    if (allow_continue) {
        ui_delay(5000);
        ui->scrolling.redraw = pico_ui_draw_continue_footer;
        pico_display_wait_for_click();
        pico_display_stop_scrolling();
        return;
    }
    for (;;)
        pico_display_update_scrolling();
}
void pico_halt_heater_malfunction(void)
{
    PicoUi *ui = pico_ui_active;
    ui_interrupts_enable();
    ui->app->control_signals.enabled = 0;
    pico_actuators_off_except_drain(ui->actuators);
    ui->services->buzzer(ui->service_context, true);
    ui_interrupts_disable();
    pico_display_clear();
    pico_ui_draw_heater_malfunction();
    pico_api_error(ui->network, 24);
    for (;;)
        pico_display_update_scrolling();
}
void pico_diagnostic_sensors(void)
{
    PicoUi *ui = pico_ui_active;
    pico_display_clear();
    pico_display_builtin_font();
    pico_ui_draw_header("SENSORS");
    pico_ui_draw_continue_hint();
    pico_display_text_at_cell(2, 0, "Step Filter: ");
    pico_display_flush();
    uint32_t last = 0;
    while (!pico_local_cancelled() && !pico_encoder_take_click(&ui->encoder)) {
        pico_display_clear_row(2);
        pico_display_text_at_cell(
            2, 0, ui_read(PICO_PIN_STEP_FILTER) ? "Step Filter: OUT " : "Step Filter: IN  ");
        if (ui_millis() - last > 1000) {
            pico_display_temperature_columns(1, ui->app->controller.wort_fahrenheit,
                                             ui->app->controller.thermoblock_fahrenheit, 0.0);
            last = ui_millis();
        }
        pico_display_flush();
    }
    pico_display_custom_font();
}
void pico_diagnostic_drain_pump(void)
{
    PicoUi *ui = pico_ui_active;
    pico_display_clear();
    pico_display_builtin_font();
    pico_ui_draw_header("Drain Pump");
    pico_actuator_drain_on(ui->actuators);
    uint32_t started = ui_millis();
    pico_ui_draw_continue_hint();
    pico_display_center_text(1, "Drain Pump active");
    while (!pico_local_cancelled() && !pico_encoder_take_click(&ui->encoder))
        pico_display_update_timer(2, (int)started, 0);
    pico_actuator_drain_off(ui->actuators);
    pico_display_custom_font();
}
static void reset_valve_screen(const char *title)
{
    pico_display_clear();
    pico_display_builtin_font();
    pico_ui_draw_header(title);
    pico_display_center_text(1, "Resetting");
    pico_display_center_text(2, "Rotary Valve");
    pico_display_flush();
    pico_ui_active->services->stepper_move(pico_ui_active->service_context, 2);
}
void pico_diagnostic_steam_pump(void)
{
    PicoUi *ui = pico_ui_active;
    reset_valve_screen("Keg Pump");
    uint32_t started = ui_millis();
    pico_actuator_steam_on(ui->actuators);
    pico_display_clear();
    pico_ui_draw_header("Keg Pump");
    pico_display_center_text(1, "Keg Pump active");
    pico_ui_draw_continue_hint();
    while (!pico_local_cancelled() && !pico_encoder_take_click(&ui->encoder)) {
        pico_actuators_pause_for_missing_filter(ui->actuators, true, false, false);
        pico_display_update_timer(2, (int)started, 0);
    }
    pico_actuator_steam_off(ui->actuators);
    pico_display_custom_font();
    ui->services->restore_idle(ui->service_context);
}
void pico_diagnostic_heater(void)
{
    PicoUi *ui = pico_ui_active;
    reset_valve_screen("THERMOBLOCK STEAM");
    pico_display_clear();
    pico_ui_draw_header("THERMOBLOCK STEAM");
    uint32_t started = ui_millis();
    ui_write(PICO_PIN_HEATER, 1);
    uint32_t last = 0;
    while (!pico_local_cancelled() && ui_millis() - started < 15000) {
        if (ui_millis() - last > 1000) {
            pico_display_temperature_columns(2, ui->app->controller.wort_fahrenheit,
                                             ui->app->controller.thermoblock_fahrenheit, 0.0);
            pico_display_flush();
            last = ui_millis();
        }
    }
    ui_write(PICO_PIN_HEATER, 0);
    pico_display_custom_font();
    ui->services->restore_idle(ui->service_context);
}
void pico_diagnostic_stepper(void)
{
    PicoUi *ui = pico_ui_active;
    pico_display_clear();
    pico_ui_draw_header("TEST ROTARY VALVE");
    pico_ui_draw_stop_footer();
    pico_display_flush();
    unsigned position = 0;
    for (;;) {
        if (pico_local_cancelled())
            break;
        ui->services->stepper_move(ui->service_context, position & 255);
        if (pico_encoder_take_click(&ui->encoder))
            break;
        position++;
        if (position > 6)
            position = 0;
    }
    pico_display_clear();
    ui->services->restore_idle(ui->service_context);
}
void pico_ui_toggle_stepper_torque(void)
{
    PicoUi *ui = pico_ui_active;
    pico_display_clear();
    pico_display_builtin_font();
    pico_ui_draw_header("SETTING HIGH TORQUE");
    pico_display_flush();
    const char *last;
    short last_row;
    if (!ui->services->stepper_torque(ui->service_context, 10)) {
        pico_display_center_text(3, "HIGH TORQUE FAIL");
        last = "COULD NOT SET";
        last_row = 4;
    } else {
        pico_display_clear();
        pico_display_center_text(1, "HIGH TORQUE SET");
        pico_display_center_text(2, "CLICK 4 STEPPER KEG");
        pico_ui_draw_continue_hint();
        pico_display_flush();
        pico_display_wait_for_click();
        ui->services->flush_compartments(ui->service_context, 1, 1);
        pico_display_clear();
        pico_ui_draw_header("SETTING LOW TORQUE");
        pico_display_center_text(1, "PASSED WITH");
        last = "HIGH TORQUE";
        last_row = 2;
    }
    pico_display_center_text(last_row, last);
    pico_ui_draw_continue_hint();
    pico_display_flush();
    pico_display_wait_for_click();
    pico_display_custom_font();
}
void pico_ui_choose_compartment(void)
{
    PicoUi *ui = pico_ui_active;
    static const char *labels[] = {"Mash", "Pass Thru", "Adj 1", "Adj 2", "Adj 4", "Adj 3", "Exit"};
    pico_display_clear();
    pico_display_builtin_font();
    pico_ui_draw_header("Keg Pump Posistion");
    for (unsigned i = 0; i < 7; i++)
        strcpy(ui->menu_labels[i], labels[i]);
    uint32_t selected = pico_menu_choose(3, 7, 0);
    if (pico_local_cancelled() || selected >= 6)
        return;
    pico_display_clear();
    pico_ui_draw_header("Keg Pump Posistion");
    pico_display_center_text(1, ui->menu_labels[selected]);
    pico_display_flush();
    ui->services->stepper_move(ui->service_context, (selected + 1) & 255);
    uint32_t started = ui_millis();
    pico_actuator_steam_on(ui->actuators);
    pico_display_clear();
    pico_ui_draw_header("Keg Pump");
    pico_display_center_text(1, "Keg Pump active");
    pico_ui_draw_continue_hint();
    while (!pico_local_cancelled() && !pico_encoder_take_click(&ui->encoder)) {
        pico_actuators_pause_for_missing_filter(ui->actuators, true, false, false);
        pico_display_update_timer(2, (int)started, 0);
    }
    pico_actuator_steam_off(ui->actuators);
    pico_display_custom_font();
    ui->services->restore_idle(ui->service_context);
}
void pico_diagnostic_shuttle_pump(void)
{
    PicoUi *ui = pico_ui_active;
    pico_display_clear();
    pico_display_center_text(0, "Resetting");
    pico_display_center_text(1, "Rotary Valve");
    pico_display_flush();
    ui->services->stepper_move(ui->service_context, 2);
    pico_display_clear();
    pico_ui_draw_header("Shuttle Pump");
    pico_actuator_shuttle_on(ui->actuators);
    pico_ui_wait_for_stop();
    pico_actuator_shuttle_off(ui->actuators);
    ui->services->restore_idle(ui->service_context);
}
int pico_diagnostic_buzzer(void)
{
    PicoUi *ui = pico_ui_active;
    pico_display_clear();
    pico_display_center_text(0, "Sounding alarm");
    pico_display_flush();
    ui->services->buzzer(ui->service_context, true);
    pico_ui_wait_for_stop();
    return ui->services->buzzer(ui->service_context, false);
}
void pico_diagnostic_rfid(void)
{
    char uid[22] = {0};
    pico_display_builtin_font();
    pico_display_clear();
    pico_ui_draw_header("RFID Detect");
    pico_ui_draw_continue_hint();
    if (!pico_ui_active->services->rfid_read_uid(pico_ui_active->service_context, uid))
        strcpy(uid, "No tag detected");
    pico_display_center_text(2, uid);
    pico_ui_draw_continue_hint();
    pico_display_flush();
    pico_display_wait_for_click();
    pico_display_custom_font();
}
void pico_diagnostic_run_all(void)
{
    pico_display_clear();
    pico_diagnostic_shuttle_pump();
    pico_diagnostic_heater();
    pico_diagnostic_sensors();
    pico_diagnostic_stepper();
    pico_ui_active->services->flush_compartments_until_press(pico_ui_active->service_context);
    pico_diagnostic_buzzer();
    pico_diagnostic_rfid();
    pico_storage_reset_shuttle_scale(pico_ui_active->network);
    pico_diagnostic_steam_pump();
    pico_diagnostic_drain_pump();
    pico_display_clear();
}
void pico_diagnostics_menu(void)
{
    for (;;) {
        pico_display_clear();
        uint32_t count = pico_menu_populate_hardware_tests();
        pico_ui_draw_header("TEST PICO");
        switch (pico_menu_choose(3, count, 0)) {
        case 0:
            pico_diagnostic_run_all();
            break;
        case 1:
            pico_diagnostic_shuttle_pump();
            break;
        case 2:
            pico_diagnostic_heater();
            break;
        case 3:
            pico_diagnostic_sensors();
            break;
        case 4:
            pico_diagnostic_stepper();
            break;
        case 5:
            pico_ui_active->services->flush_compartments_until_press(
                pico_ui_active->service_context);
            break;
        case 6:
            pico_diagnostic_buzzer();
            break;
        case 7:
            pico_diagnostic_rfid();
            break;
        case 8:
            pico_storage_reset_shuttle_scale(pico_ui_active->network);
            break;
        case 9:
            pico_diagnostic_steam_pump();
            break;
        case 10:
            pico_diagnostic_drain_pump();
            break;
        case 11:
            pico_ui_toggle_stepper_torque();
            break;
        case 12:
            pico_ui_active->services->heat_flush_compartments(pico_ui_active->service_context, 1,
                                                              320.0);
            break;
        case 13:
            pico_ui_choose_compartment();
            break;
        case 14:
            pico_screen_show_help_temporarily();
            break;
        default:
            pico_display_custom_font();
            return;
        }
    }
}
void pico_diagnostic_wifi(void)
{
#if PICO_STANDALONE
    /* This profile has no internet dependency to diagnose. Show the actual
     * local address and connection status without pinging an external host. */
    pico_ui_show_network_addresses();
#else
    PicoNetwork *n = pico_ui_active->network;
    pico_display_clear();
    if (n->platform->wifi_status(n->platform_context) != 3)
        pico_network_ensure_connection(n);
    if (n->platform->wifi_status(n->platform_context) != 3) {
        pico_display_clear();
        pico_screen_wifi_connection_failed();
        pico_ui_wait_for_continue();
        return;
    }
    pico_ui_show_wifi_test_result(0);
    int status = pico_network_check_status(n, false, true);
    ui_delay(500);
    pico_display_clear();
    if (status == 3)
        pico_ui_show_wifi_test_result(1);
    else if (status == 9 || status == 10) {
        pico_display_clear();
        pico_ui_draw_wifi_test_heading();
        pico_screen_wifi_connection_failed();
        pico_ui_wait_for_continue();
        return;
    }
    if (pico_network_ipv4_is_zero(n->platform->local_ip(n->platform_context))) {
        pico_ui_show_wifi_test_result(2);
        return;
    }
    pico_ui_show_wifi_test_result(3);
    char line[24];
    snprintf(line, sizeof line, "%u of 5 returned",
             (unsigned)n->platform->ping_public_dns(n->platform_context));
    pico_display_clear_row(1);
    pico_display_center_text(1, line);
    pico_display_flush();
    ui_delay(1500);
    pico_ui_show_wifi_test_result(4);
    pico_ui_show_wifi_test_result(5);
    if (!pico_api_error(n, 17)) {
        pico_display_clear_row(1);
        pico_display_center_text(1, "PicoBrew: NO");
        pico_display_flush();
        ui_delay(2000);
    }
    pico_display_flush();
    ui_delay(2000);
    pico_ui_wait_for_stop();
    pico_display_clear();
#endif
}
void pico_ui_show_network_addresses(void)
{
    PicoNetwork *n = pico_ui_active->network;
    pico_display_clear();
#if PICO_STANDALONE
    if (n->platform->wifi_status(n->platform_context) != 3 &&
        !pico_network_ensure_connection(n)) {
        pico_screen_wifi_connection_failed();
        pico_display_flush();
        pico_display_wait_for_click();
        return;
    }
#else
    if (n->platform->wifi_status(n->platform_context) != 3)
        pico_network_ensure_connection(n);
#endif
    PicoIpv4 addresses[4] = {0};
    n->platform->ip_configuration(n->platform_context, &addresses[0], &addresses[1], &addresses[2],
                                  &addresses[3]);
#if PICO_STANDALONE
    if (pico_network_ipv4_is_zero(addresses[0])) {
        pico_display_clear();
        pico_screen_wifi_connection_failed();
        pico_display_flush();
        pico_display_wait_for_click();
        return;
    }
#endif
    pico_display_clear();
    char ssid[19];
    strncpy(ssid, n->settings->wifi_ssid, 18);
    ssid[18] = 0;
    pico_display_center_text(0, ssid);
    pico_ui_wait_for_label_choice("WIFI STATUS");
    static const char *labels[] = {"IP ADDRESS", "DNS ADDRESS", "GATEWAY", "SUBNET MASK"};
    for (unsigned i = 0; i < 4; i++) {
        char line[16];
        snprintf(line, sizeof line, "%u.%u.%u.%u", addresses[i].octet[0],
                 addresses[i].octet[1], addresses[i].octet[2], addresses[i].octet[3]);
        pico_display_clear();
        pico_display_text_at_cell(0, 0, line);
        pico_ui_wait_for_label_choice(labels[i]);
    }
}
void pico_alarm_until_press_or_timeout(uint32_t language, uint32_t status_selector, int timeout_ms)
{
    PicoUi *ui = pico_ui_active;
    pico_ui_draw_footer_single(pico_action_label(49, language, status_selector));
    pico_display_flush();
    ui_log("I", "SCR", "playAlarmUntilEncoderPressOrTimeout()");
    uint32_t deadline = ui_millis() + (uint32_t)timeout_ms;
    ui_log("I", "SCR", "playAlarmWhile()");
    ui_assert(true, "predicate != NULL");
    ui->services->buzzer(ui->service_context, false);
    bool on = false;
    uint32_t next_toggle = 0;
    for (;;) {
        uint32_t now = ui_millis();
        if (pico_local_cancelled() || pico_encoder_take_click(&ui->encoder) || deadline < now)
            break;
        pico_display_update_scrolling();
        now = ui_millis();
        if (next_toggle < now) {
            ui->services->buzzer(ui->service_context, !on);
            next_toggle = now + (on ? 4000 : 750);
            on = !on;
        }
    }
    if (on)
        ui->services->buzzer(ui->service_context, false);
}
