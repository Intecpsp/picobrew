#!/usr/bin/env python3
"""Execute C service/diagnostic functions from OEM and the complete rebuilt ELF.

Compare ordered drawing, input, timing, actuator and network boundary effects.
No electrical behavior, interrupt scheduling or physical hardware is simulated.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from ui_linked_support import Pair, Layout, ROOT, argument

ENTRIES = {
    'pico_diagnostic_sensors': 0x2000a4b0,
    'pico_diagnostic_drain_pump': 0x2000c2f8,
    'pico_diagnostic_steam_pump': 0x2000c814,
    'pico_diagnostic_heater': 0x2000c854,
    'pico_diagnostic_stepper': 0x2000c904,
    'pico_ui_toggle_stepper_torque': 0x2000ca04,
    'pico_ui_choose_compartment': 0x2000de2c,
    'pico_diagnostic_shuttle_pump': 0x2000e920,
    'pico_diagnostic_buzzer': 0x2000e96c,
    'pico_diagnostic_rfid': 0x20009e4c,
    'pico_ui_confirm_still_cleaned': 0x20009af0,
    'pico_ui_show_still_communication_failure': 0x20009ab8,
    'pico_ui_show_still_completion': 0x2000e3b0,
    'pico_ui_show_still_temperature_warning': 0x2000e3d0,
    'pico_ui_still_fan_menu': 0x2000f570,
    'pico_diagnostic_wifi': 0x2000ee20,
    'pico_ui_show_network_addresses': 0x2000ef34,
    'pico_diagnostic_run_all': 0x2000e994,
    'pico_diagnostics_menu': 0x2000e9cc,
    'pico_report_error': 0x2000b9d8,
    'pico_alarm_until_press_or_timeout': 0x20012544,
}


def mechanical(p, clicks=3, step=401, pin=0):
    p.clock(step)
    p.clicks(clicks)
    p.service('stepper_move', 'pico_stepper_move_to_compartment', 1)
    p.service('restore_idle', 'pico_restore_idle_hardware')
    for kind in ('drain', 'steam', 'shuttle'):
        for state in ('on', 'off'):
            p.hook('pico_actuator_' + kind + '_' + state, 'pico_' + kind + '_pump_' + state)
    p.hook('pico_actuators_pause_for_missing_filter', 'pico_pause_outputs_for_missing_filter', count=3, new_shift=1)
    for method, entry, count, result in [('digital_read', 0x20017f10, 1, pin), ('digital_write', 0x20017f40, 2, 0)]:
        def observed(r, side, shift, method=method, count=count, result=result):
            p.event(side, method, *[argument(r, i + shift) for i in range(count)])
            return result
        p.old.hook(p.entry(entry), lambda r, fn=observed: fn(r, 'old', 0))
        p.callback('PicoUiHardware', method, lambda r, fn=observed: fn(r, 'new', 1))
    p.old.write(0x2002bc30, struct.pack('<dd', 153.75, 290.25))
    p.write('state', 'PicoAppState', 'controller.wort_fahrenheit', 153.75, 'd')
    p.write('state', 'PicoAppState', 'controller.thermoblock_fahrenheit', 290.25, 'd')
    def temperatures(r, side):
        p.event(side, 'temperature_columns', r.reg(0),
                struct.pack('<II', r.reg(2), r.reg(3)).hex(), r.read(r.reg('sp'), 16).hex())
    p.old.hook(p.entry(0x2000a358), lambda r: temperatures(r, 'old'))
    p.new.hook_symbol('pico_display_temperature_columns', lambda r: temperatures(r, 'new'))


def network(p, statuses, check=0, ip=0x04030201, ping=0, reported=0):
    indices = {'old': 0, 'new': 0}
    def status(side):
        index = indices[side]
        indices[side] += 1
        value = statuses[min(index, len(statuses) - 1)]
        p.event(side, 'wifi_status', value)
        return value
    p.old.hook(p.addresses['WiFi_status'], lambda r: status('old'))
    p.callback('PicoNetworkPlatform', 'wifi_status', lambda r: status('new'))
    p.hook('pico_network_ensure_connection', 'pico_wifi_ensure_connection', returns=1)
    p.hook('pico_network_check_status', 'pico_wifi_check_status', count=2, new_shift=1, returns=check)
    def local(r, side):
        p.event(side, 'local_ip', ip)
        if side == 'old':
            r.write(r.reg(0), struct.pack('<II', 0, ip))
        else:
            return ip
    p.old.hook(p.addresses['WiFi_get_local_ip'], lambda r: local(r, 'old'))
    p.callback('PicoNetworkPlatform', 'local_ip', lambda r: local(r, 'new'))
    p.old.hook(p.addresses['pico_wifi_ping_public_dns'], lambda r: (p.event('old', 'ping', ping), ping)[1])
    p.callback('PicoNetworkPlatform', 'ping_public_dns', lambda r: (p.event('new', 'ping', ping), ping)[1])
    p.hook('pico_api_error', 'pico_api_report_error', count=1, new_shift=1, returns=reported)
    p.old.hook(p.entry(0x20017d4c), lambda r: p.event('old', 'delay', r.reg(0)))
    p.callback('PicoUiHardware', 'delay_ms', lambda r: p.event('new', 'delay', r.reg(1)))


def verify(variant, elf):
    layout = Layout(elf)
    traces = []
    def run(name, label, setup, language=0, args=()):
        p = Pair(variant, elf, layout, language)
        p.drawing()
        setup(p)
        trace = p.invoke(name, ENTRIES[name], args=args, compare_return=name == 'pico_diagnostic_buzzer')
        traces.append([name, label, language, trace])
    for name in ('pico_diagnostic_drain_pump', 'pico_diagnostic_steam_pump',
                 'pico_diagnostic_heater', 'pico_diagnostic_stepper', 'pico_diagnostic_shuttle_pump'):
        for clicks in (1, 3, 10):
            run(name, clicks, lambda p, n=clicks: mechanical(p, n, 400 if name == 'pico_diagnostic_heater' else 401))
    for pin in (0, 1):
        for step in (0, 401, 1001):
            run('pico_diagnostic_sensors', [pin, step], lambda p, pin=pin, step=step: mechanical(p, 4, step, pin))
    for success in (0, 1):
        def torque(p):
            p.service('stepper_torque', 'pico_stepper_set_torque_mode', 1, success)
            p.service('flush_compartments', 'pico_flush_compartments', 2)
        run('pico_ui_toggle_stepper_torque', success, torque)
        def buzzer(p):
            p.old.hook(p.addresses['pico_buzzer_on'], lambda r: (p.event('old', 'buzzer', 1), 1)[1])
            p.old.hook(p.addresses['pico_buzzer_off'], lambda r: (p.event('old', 'buzzer', 0), success)[1])
            p.callback('PicoUiServices', 'buzzer', lambda r: (p.event('new', 'buzzer', r.reg(1)), 1 if r.reg(1) else success)[1])
        run('pico_diagnostic_buzzer', success, buzzer)
        for uid in (b'01234567', b'0123456789abcdef0123'):
            def rfid(p):
                def read(r, side, index):
                    pointer = r.reg(index)
                    if r.read(pointer, 21) != bytes(21):
                        raise AssertionError('RFID destination not initialized')
                    if success:
                        r.write(pointer, uid + b'\0')
                    p.event(side, 'read_uid', success, uid.hex() if success else '')
                    return success
                p.old.hook(p.addresses['pico_rfid_read_uid'], lambda r: read(r, 'old', 0))
                p.callback('PicoUiServices', 'rfid_read_uid', lambda r: read(r, 'new', 1))
            run('pico_diagnostic_rfid', [success, uid.hex()], rfid)
    for selected in range(7):
        def compartment(p):
            mechanical(p)
            p.hook('pico_menu_choose', count=3, returns=selected)
        run('pico_ui_choose_compartment', selected, compartment)
    for name in ('pico_ui_show_still_completion', 'pico_ui_show_still_temperature_warning'):
        for language in (0, 1, 2):
            run(name, 'prompt', lambda p: p.service('alarm_until_press', 'pico_alarm_until_encoder_press'), language)
    for name in ('pico_ui_confirm_still_cleaned', 'pico_ui_show_still_communication_failure'):
        def prompt(p):
            for side, arm in [('old', p.old), ('new', p.new)]:
                callback = p.entry(0x2000b2b8 if name == 'pico_ui_confirm_still_cleaned' else 0x2000b2cc) if side == 'old' else arm.address('pico_ui_draw_confirm_footer' if name == 'pico_ui_confirm_still_cleaned' else 'pico_ui_draw_continue_footer')
                def called(r, side=side, callback=callback):
                    if (r.reg(1) & ~1) != (callback & ~1):
                        raise AssertionError('Wrong scrolling footer')
                    p.event(side, 'scroll_message', r.cstring(r.reg(0)).hex())
                arm.hook(p.entry(0x20009a84) if side == 'old' else arm.address('pico_ui_show_scrolling_message'), called)
        run(name, 'scrolling prompt', prompt)
    for selected in (0, 1, 2):
        def fan(p):
            p.hook('pico_menu_choose_default_timeout', count=6, returns=selected)
            p.hook('pico_still_set_fan', count=1, new_shift=1, returns=1)
            p.hook('pico_display_stop_progress_indicator', 0x20009910)
        run('pico_ui_still_fan_menu', selected, fan)
    scenarios = [([0,0],0,0,0,0),([0,3],3,0,0,0),([3,3],9,0x04030201,0,0),
                 ([3,3],10,0x04030201,0,0),([3,3],3,0x04030201,0,0),
                 ([3,3],3,0x04030201,5,1),([3,3],0,0xffffffff,3,0)]
    for language in (0,1,2):
        for case in scenarios:
            run('pico_diagnostic_wifi', case, lambda p, case=case: network(p, *case), language)
    for connected in (0,3):
        for ssid in (b'WiFi', b'A network longer than eighteen chars'):
            values = [0x0201a8c0, 0x08080808, 0x0101a8c0, 0x00ffffff]
            def addresses(p):
                network(p, [connected])
                p.old.write(0x2002bd04, ssid + b'\0')
                p.write('settings', 'PicoSettings', 'wifi_ssid', ssid + b'\0')
                def ipconfig(r, side):
                    for i,v in enumerate(values):
                        r.write_u32(argument(r, i+1) + (4 if side == 'old' else 0), v)
                    p.event(side, 'ip_configuration', *values)
                p.old.hook(p.addresses['WiFi_get_ip_configuration'], lambda r: ipconfig(r, 'old'))
                p.callback('PicoNetworkPlatform', 'ip_configuration', lambda r: ipconfig(r, 'new'))
                p.hook('pico_ui_wait_for_label_choice', 0x2000e63c, count=1, strings=(0,))
            run('pico_ui_show_network_addresses', [connected, ssid.decode()], addresses)
    def dispatch(p, include_all=True):
        for name in ('pico_diagnostic_shuttle_pump', 'pico_diagnostic_heater', 'pico_diagnostic_sensors',
                     'pico_diagnostic_stepper', 'pico_diagnostic_buzzer', 'pico_diagnostic_rfid',
                     'pico_diagnostic_steam_pump', 'pico_diagnostic_drain_pump',
                     'pico_ui_toggle_stepper_torque', 'pico_ui_choose_compartment'):
            p.hook(name, ENTRIES[name])
        if include_all:
            p.hook('pico_diagnostic_run_all', ENTRIES['pico_diagnostic_run_all'])
        p.hook('pico_screen_show_help_temporarily', 0x20007b00)
        p.hook('pico_storage_reset_shuttle_scale', 'pico_reset_shuttle_scale')
        p.service('flush_compartments_until_press', 'pico_flush_compartments_until_press')
        def heat(r, side):
            p.event(side, 'heat_flush', r.reg(0 if side == 'old' else 1), r.reg(2), r.reg(3))
        p.old.hook(p.entry(0x2000ccc0), lambda r: heat(r, 'old'))
        p.callback('PicoUiServices', 'heat_flush_compartments', lambda r: heat(r, 'new'))
    run('pico_diagnostic_run_all', 'ordered chain', lambda p: dispatch(p, False))
    for selected in [*range(16), 99]:
        def menu(p):
            dispatch(p)
            p.hook('pico_menu_choose', count=3, sequence=[selected, 15])
        run('pico_diagnostics_menu', selected, menu)
    for code in (7, 8, 24):
        for reported in (0,1):
            for status in (0,3):
                def report(p):
                    network(p, [status])
                    p.hook('pico_display_stop_progress_indicator', 0x20009910)
                    p.hook('pico_ui_draw_error', 0x20007214, count=1)
                run('pico_report_error', [code, reported, status], report, args=[code,1,reported])
    for language in (0,1,2):
        for timeout in (0,750,5000,60000):
            for start in (0,0xffffff00):
                def alarm(p):
                    p.clock(401, start)
                    p.clicks(10)
                    p.hook('pico_display_update_scrolling', 0x20009974)
                    p.old.hook(p.addresses['pico_buzzer_on'], lambda r: (p.event('old', 'buzzer', 1), 1)[1])
                    p.old.hook(p.addresses['pico_buzzer_off'], lambda r: (p.event('old', 'buzzer', 0), 1)[1])
                    p.callback('PicoUiServices', 'buzzer', lambda r: (p.event('new', 'buzzer', r.reg(1)), 1)[1])
                run('pico_alarm_until_press_or_timeout', [timeout,start], alarm, language, [language,0,timeout])
    return {'variant': variant, 'cases': len(traces), 'functions': sorted(set(t[0] for t in traces)),
            'trace_sha256': hashlib.sha256(json.dumps(traces).encode()).hexdigest(),
            'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(), 'complete_linked_elf': True,
            'original_code_executed': True, 'rebuilt_arm_code_executed': True, 'hardware_tested': False,
            'limits': ['Explicit drawing/input/timing/network/actuator boundaries',
                       'No physical timing, interrupt interleaving or peripheral qualification']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--variant', choices=['ssd1325','ssd1309'], required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = verify(args.variant, args.elf)
    content = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(content)
    print(content, end='')


if __name__ == '__main__':
    main()
