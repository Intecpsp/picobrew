#!/usr/bin/env python3
"""Compare C-specific procedures with both original C ARM images.

The procedure body executes unchanged in the original image. UI, actuator,
stepper and clock calls are explicit recorded boundaries. This establishes
procedure ordering/state behavior for scripted inputs, not thermal or physical
qualification. The rebuilt executable has no access to the original image.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from original_arm import ArmFirmware, IMAGE_SHA256, VARIANT, words_from_double
from rebuilt_arm import RebuiltArm
from entry_points import variant_address

CONTROLLER = 0x2002ba40
MARQUEE_ENABLED = 0x2002c3f0


def u32(value):
    return value & 0xffffffff


class Procedure:
    def __init__(self, rebuilt, elf):
        self.rebuilt = rebuilt
        self.arm = RebuiltArm(elf) if rebuilt else ArmFirmware()
        a = self.arm
        if rebuilt:
            self.offsets = struct.unpack('<17I', a.read(a.address('recipe_fixture_offsets'), 68))
            self.state = a.address('recipe_fixture_state')
            self.controller = self.state + self.offsets[1]
            self.runtime = a.address('recipe_fixture_runtime')
        else:
            self.controller = CONTROLLER
        # A function-boundary comparison deliberately does not replace internal
        # branches, arithmetic, byte counters or controller-state accesses.
        boundaries = [
            ('millis', 0x20017d40, 'recipe_fixture_millis', 0, 0),
            ('all_off', 0x20006310, 'pico_actuators_all_off', 0, 0),
            ('off_except_drain', 0x20006336, 'pico_actuators_off_except_drain', 0, 0),
            ('steam_on', 0x2000c18c, 'pico_actuator_steam_on', 0, 0),
            ('steam_off', 0x2000629a, 'pico_actuator_steam_off', 0, 0),
            ('drain_on', 0x2000c1b8, 'pico_actuator_drain_on', 0, 0),
            ('drain_off', 0x200062a2, 'pico_actuator_drain_off', 0, 0),
            ('both_off', 0x200062b2, 'pico_actuators_steam_and_drain_off', 0, 0),
            ('heater_on', 0x200062c8, 'pico_actuator_heater_on', 0, 0),
            ('heater_off', 0x200062d0, 'pico_actuator_heater_off', 0, 0),
            ('shuttle_on', 0x2000c34c, 'pico_actuator_shuttle_on', 0, 0),
            ('shuttle_off', 0x200062aa, 'pico_actuator_shuttle_off', 0, 0),
            ('pause', 0x2000c378, 'pico_actuators_pause_for_missing_filter', 3, 1),
            ('move', 0x2000bf1c, 'recipe_fixture_move', 1, 1),
            ('restore_idle', 0x2000c6c4, 'pico_recipe_restore_idle', 0, 0),
            ('click', 0x200045e2, 'recipe_fixture_take_click', 0, 0),
            ('timer', 0x20009ee4, 'recipe_fixture_timer', 3, 1),
            ('center', 0x200054ac, 'recipe_fixture_center_text', 2, 1),
            ('valve', 0x2000a154, 'recipe_fixture_valve_position', 2, 1),
            ('error', 0x2000b9d8, 'recipe_fixture_error', 3, 1),
        ]
        for name, address, symbol, argc, native_shift in boundaries:
            callback = lambda arm, n=name, count=argc, shift=native_shift: self.boundary(
                n, [self.argument(i + (shift if rebuilt else 0)) for i in range(count)])
            if rebuilt:
                a.hook_symbol(symbol, callback)
            else:
                a.hook(variant_address(address), callback)
        if rebuilt:
            a.hook_symbol('recipe_fixture_screen', self.screen)
            a.hook_symbol('recipe_fixture_prompt', self.prompt)
            a.hook_symbol('recipe_fixture_clear_row',
                          lambda arm: self.boundary('clear_row', [arm.reg(1), arm.reg(2)]))
            a.hook_symbol('recipe_fixture_temperature_columns', self.temperature_columns)
        else:
            for address, name in ((0x20004488, 'clear'), (0x20004494, 'flush'),
                                  (0x200053f0, 'builtin_font'), (0x200053fc, 'custom_font'),
                                  (0x20006de0, 'continue_hint'), (0x2000b270, 'done_footer')):
                a.hook(variant_address(address),
                       lambda arm, n=name: self.boundary(n, []))
            a.hook(variant_address(0x20006d84),
                   lambda arm: self.boundary('header', [arm.cstring(arm.reg(0)).decode()]))
            a.hook(variant_address(0x20009944),
                   lambda arm: self.boundary('clear_row', [arm.reg(0), 0]))
            a.hook(variant_address(0x2000dee4),
                   lambda arm: self.boundary('vacuum_prompt', [arm.reg(0)]))
            a.hook(variant_address(0x2000a358), self.temperature_columns)

    def argument(self, index):
        return self.arm.reg(index) if index < 4 else self.arm.read_u32(
            self.arm.reg('sp') + (index - 4) * 4)

    def set_temperature(self, value):
        self.temperature = value
        self.arm.write(self.controller + 0x1f8, struct.pack('<d', value))

    def boundary(self, name, arguments):
        result = 0
        if name == 'millis':
            result = self.tick
            self.tick = u32(self.tick + self.case.get('tick_step', 1000))
            if self.heating and self.case.get('warm_per_tick'):
                self.set_temperature(min(self.case.get('warm_limit', 240.),
                                         self.temperature + self.case['warm_per_tick']))
            arguments = [result]
        elif name == 'pause':
            self.pause_count += 1
            result = int(self.pause_count in self.case.get('pauses', []))
            arguments = [*arguments, result]
        elif name == 'click':
            self.click_count += 1
            threshold = self.case.get('click_after', 1)
            result = int(threshold is not None and self.click_count >= threshold)
            arguments = [result]
        elif name == 'vacuum_prompt':
            result = self.case.get('accepted', 1) if arguments[0] == 0 else 0
        elif name == 'center':
            arguments[1] = self.arm.cstring(arguments[1]).decode()
        elif name == 'heater_on':
            self.heating = True
        elif name == 'heater_off':
            self.heating = False
        elif name == 'all_off' or name == 'off_except_drain':
            self.heating = False
        elif name == 'shuttle_on':
            self.shuttle = True
        elif name == 'shuttle_off':
            self.shuttle = False
        elif name == 'move' and self.case.get('reset_temperature_on_move'):
            self.set_temperature(self.case['temperature'])
        self.events.append([name, *arguments])
        return result

    def screen(self, arm):
        screen, argument, text = arm.reg(1), arm.reg(2), arm.reg(3)
        if screen == 43:  # Direct OEM enabled-byte write, compared as state.
            self.scrolling = 0
            return 0
        names = {0: 'clear', 1: 'flush', 28: 'stop_footer', 29: 'header',
                 40: 'builtin_font', 41: 'custom_font', 44: 'done_footer', 46: 'continue_hint'}
        name = names[screen]
        return self.boundary(name, [arm.cstring(text).decode()] if screen == 29 else [])

    def prompt(self, arm):
        assert arm.reg(1) == 6, ('unexpected prompt', arm.reg(1))
        return self.boundary('vacuum_prompt', [arm.reg(2)])

    def temperature_columns(self, arm):
        row = arm.reg(1) if self.rebuilt else arm.reg(0)
        values = [struct.unpack('<d', struct.pack('<II', self.argument(i),
                                                 self.argument(i + 1)))[0]
                  for i in (2, 4, 6)]
        self.boundary('temperature_columns', [row, *values])
        if self.shuttle and self.case.get('cool_per_display'):
            self.set_temperature(self.temperature - self.case['cool_per_display'])
        return 0

    def run(self, procedure, case):
        self.case = case
        self.events = []
        self.tick = case.get('tick_start', 0)
        self.click_count = self.pause_count = 0
        self.heating = self.shuttle = False
        self.scrolling = 1
        a = self.arm
        if self.rebuilt:
            a.call_symbol('recipe_fixture_reset')
        else:
            a.write(CONTROLLER, bytes(0x228))
            a.write(MARQUEE_ENABLED, b'\x01')
        self.set_temperature(case.get('temperature', 225.))
        a.write(self.controller + 0x1f0, struct.pack('<d', 72.))
        pointer = a.SCRATCH
        a.write(pointer, bytes([case.get('next', 1)]))
        args = {
            'vacuum': [0, 1],
            'flush_compartments': [case.get('minimum_cycles', 0), case.get('automatic', 0)],
            'c_drain_sequence': [pointer, case.get('first', 1)],
            'heat_flush_compartments': [case.get('steam', 1), *words_from_double(case.get('target', 240.))],
        }[procedure]
        if self.rebuilt:
            symbol = 'pico_recipe_' + procedure
            call_args = [self.runtime, *args]
            if procedure == 'heat_flush_compartments':
                call_args = [self.runtime, case.get('steam', 1),
                             *words_from_double(case.get('target', 240.))]
            result = a.call_symbol(symbol, call_args, max_instructions=1000000)
        else:
            address = {'vacuum': 0x2000e200, 'flush_compartments': 0x2000c948,
                       'c_drain_sequence': 0x2000dfb8,
                       'heat_flush_compartments': 0x2000ccc0}[procedure]
            call_args = args
            if procedure == 'c_drain_sequence':
                call_args = [0, *args]
            elif procedure == 'heat_flush_compartments':
                call_args = [case.get('steam', 1), 0, *words_from_double(case.get('target', 240.))]
            result = a.call(variant_address(address), call_args, max_instructions=1000000)
            self.scrolling = a.read(MARQUEE_ENABLED, 1)[0]
        return {'events': self.events, 'next': a.read(pointer, 1)[0],
                'scrolling': self.scrolling,
                'temperature': a.read(self.controller + 0x1f8, 8).hex()}, result['instructions']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT / 'build/recipe/application-validation.elf')
    parser.add_argument('--output', type=Path,
                        default=ROOT / ('analysis/recipe-flows-' + VARIANT + '-tests.json'))
    args = parser.parse_args()
    original, rebuilt = Procedure(False, args.elf), Procedure(True, args.elf)
    cases = [('vacuum', {'accepted': 0})]
    for click in (1, 2, 5, 21):
        for pauses in ([], [1], [1, 3]):
            cases.append(('vacuum', {'click_after': click, 'pauses': pauses}))
    for cycles in (0, 1, 2):
        for automatic in (0, 1):
            for click in (1, 7, 30):
                cases.append(('flush_compartments', {'minimum_cycles': cycles,
                                                     'automatic': automatic, 'click_after': click}))
    for temperature in (80., 209., 210., 219., 220., 225., 230., 231., 260.):
        for first in (1, 2):
            for pauses in ([], [1, 3, 8]):
                case = {'temperature': temperature, 'first': first, 'next': first,
                        'tick_step': 10001, 'click_after': 7, 'pauses': pauses}
                if 90. <= temperature < 210.:
                    case.update(warm_per_tick=5., warm_limit=225.)
                cases.append(('c_drain_sequence', case))
    cases += [('c_drain_sequence', {'temperature': 225., 'tick_step': 20001,
                                    'click_after': None}),
              ('c_drain_sequence', {'temperature': 225., 'tick_step': 10001,
                                    'click_after': 3, 'tick_start': 0xfffff000})]
    for steam in (0, 1):
        for temperature in (80., 200., 230., 240., 260.):
            for target in (220., 240.9):
                case = {'temperature': temperature, 'target': target, 'steam': steam,
                        'tick_step': 5001, 'cool_per_display': 10.,
                        'reset_temperature_on_move': True}
                if temperature >= 90.:
                    case.update(warm_per_tick=10., warm_limit=target + 10.)
                cases.append(('heat_flush_compartments', case))
    records = []
    for index, (procedure, case) in enumerate(cases):
        expected, original_count = original.run(procedure, case)
        actual, rebuilt_count = rebuilt.run(procedure, case)
        if actual != expected:
            failure = {'procedure': procedure, 'case': case, 'expected': expected, 'actual': actual}
            path = ROOT / 'build/recipe/recipe-flow-failure.json'
            path.write_text(json.dumps(failure, indent=2) + '\n')
            for i, (left, right) in enumerate(zip(expected['events'], actual['events'])):
                if left != right:
                    raise AssertionError(f'{index} {procedure} event {i}: {left!r} != {right!r}; {path}')
            raise AssertionError(f'{index} {procedure} state/event count mismatch; {path}')
        records.append({'procedure': procedure, 'inputs': case, 'events': len(expected['events']),
                        'original_instructions': original_count,
                        'rebuilt_instructions': rebuilt_count})
    report = {'schema_version': 1, 'passed': len(records), 'variant': VARIANT,
              'original_sha256': IMAGE_SHA256,
              'rebuilt_elf_sha256': hashlib.sha256(args.elf.read_bytes()).hexdigest(),
              'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [ROOT / 'src/recipe_utilities.c', Path(__file__),
                                          ROOT / 'tests/recipe_fixture.c']},
              'scope': 'Original ARM versus rebuilt ARM procedure ordering and state with explicit UI, clock, actuator and stepper boundaries.',
              'physical_qualification': False, 'cases': records}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{VARIANT}: {len(records)} C-specific recipe flow cases passed')


if __name__ == '__main__':
    main()
