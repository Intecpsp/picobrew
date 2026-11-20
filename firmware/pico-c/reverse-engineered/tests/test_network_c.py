#!/usr/bin/env python3
"""C-specific rinse marker and firmware-update orchestration comparisons.

UI event expansion is a documented boundary: separate UI tests cover its
localized screen/menu composition. No network, flash or reset is executed.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import sys
import test_network as base
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from original_arm import VARIANT, IMAGE_SHA256
from network_test_addresses import MAP, address


class ResetReached(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=base.ELF)
    parser.add_argument('--output', type=Path, default=ROOT / ('analysis/network-c-' + VARIANT + '-tests.json'))
    options = parser.parse_args()
    base.ELF = options.elf
    delta = -24 if VARIANT == 'ssd1309' else 0
    records, repairs = [], []
    for present in (0, 1):
        old, new = base.old(), base.Harness()
        events = [[], []]
        def original_read(f):
            events[0].append(('read', f.cstring(f.reg(0)), f.reg(2)))
            return present
        def recovered_read(f):
            # Typed capacity includes the terminator; original max_count does not.
            events[1].append(('read', f.cstring(f.reg(1)), f.reg(3) - 1))
            return present
        old.hook(MAP['pico_storage_read_text'], original_read)
        # GCC splits the checked public entry into a .part.0 helper and calls
        # that helper after proving the arguments non-null. The disassembly
        # retains the same (network, name, buffer, capacity) register ABI.
        for name in new.f.symbols:
            if name == 'pico_storage_read_text' or name.startswith('pico_storage_read_text.part.'):
                new.f.hook_symbol(name, recovered_read)
        expected = old.call(0x2000b448 + delta)['r0']
        actual = new.call('pico_storage_first_rinse_required')
        assert expected == actual == 1 - present and events[0] == events[1], (present, expected, actual, events)
        records.append(['first_rinse_required', present, expected, events[0][0][1].decode(), events[0][0][2]])
    for saved in (0, 1):
        old, new = base.old(), base.Harness()
        events = [[], []]
        old.hook(MAP['pico_file_write_text'], lambda f: (events[0].append(
            (f.cstring(f.reg(0)), f.cstring(f.reg(1)), f.reg(2))), saved)[1])
        new.f.hook_symbol('pico_storage_write_text', lambda f: (events[1].append(
            (f.cstring(f.reg(1)), f.cstring(f.reg(2)), f.reg(3))), saved)[1])
        old.call(0x200059d0 + delta)
        new.call('pico_storage_save_first_rinse_complete')
        assert events[0] == events[1] == [(b'rinse.txt', b'd', 10)]
        records.append(['save_first_rinse_complete', saved, 'rinse.txt', 'd', 10])
    headings = ['Pico C', 'Error de', 'Echec de la ']
    labels = ['Failed update', 'actualizacion', 'mise a jour']
    for available, downloaded, slot_written, language in itertools.product((0, 1), (0, 1), (0, 1), range(3)):
        old, new = base.old(), base.Harness()
        events = [[], []]
        def emit(side, tag, *args):
            events[side].append((tag, *args))
            return 0
        def paired(original_name, new_name, tag, value):
            old.hook(MAP[original_name], lambda f: (emit(0, tag), value)[1])
            new.f.hook_symbol(new_name, lambda f: (emit(1, tag), value)[1])
        paired('pico_check_firmware_update', 'pico_update_available', 'check', available)
        paired('pico_download_firmware', 'pico_update_download', 'download', downloaded)
        old.write(0x2002a90c, bytes([language]))
        old.hook(0x20004488, lambda f: emit(0, 'clear'))
        old.hook(0x20004494, lambda f: emit(0, 'flush'))
        old.hook(MAP['pico_display_logo'], lambda f: emit(0, 'updating'))
        old.hook(0x2000a810 + delta, lambda f: 0)
        def clear_rows(f):
            assert [f.reg(0), f.reg(1)] == [4, 7]
            return 0
        old.hook(MAP['pico_display_clear_rows'], clear_rows)
        old.hook(0x2000a84c + delta, lambda f: emit(0, 'downloading'))
        heading = []
        old.hook(0x200054ac + (-20 if VARIANT == 'ssd1309' else 0),
                 lambda f: heading.append(f.cstring(f.reg(1)).decode()))
        old.hook(0x2000e63c + delta, lambda f: emit(0, 'failed', heading[-1], f.cstring(f.reg(0)).decode()))
        old.hook(MAP['pico_api_report_error'], lambda f: emit(0, 'error', f.reg(0)))
        new.f.hook_symbol('pico_api_error', lambda f: emit(1, 'error', f.reg(1)))
        old.hook(MAP['WiFi_disconnect'], lambda f: emit(0, 'disconnect'))
        new.hook('disconnect', lambda f: emit(1, 'disconnect'))
        old.hook(address(0x20018f08), lambda f: emit(0, 'delay', f.reg(0)))
        new.hook('delay', lambda f: emit(1, 'delay', f.reg(1)))
        old.hook(MAP['boot_info_toggle_application_slot'],
                 lambda f: (emit(0, 'slot', f.reg(0)), slot_written)[1])
        new.f.hook_symbol('pico_boot_toggle_application',
                         lambda f: (emit(1, 'slot', f.reg(1)), slot_written)[1])
        def reset(side):
            emit(side, 'reset')
            raise ResetReached()
        old.hook(MAP['PRCMMCUReset'], lambda f: reset(0))
        new.hook('reset', lambda f: reset(1))
        def ui(f):
            kind = f.reg(1)
            if kind in (0, 4, 9, 10):
                emit(1, {0: 'clear', 4: 'flush', 9: 'updating', 10: 'downloading'}[kind])
            elif kind == 11:
                if language == 1:
                    emit(1, 'clear')
                emit(1, 'failed', headings[language], labels[language])
            else:
                raise AssertionError(('unexpected UI event', kind))
        new.hook('event', ui)
        for machine, callback in ((old, lambda: old.call(0x20012230 + delta)),
                                  (new.f, lambda: new.call('pico_update_run'))):
            try:
                callback()
            except ResetReached:
                pass
        case = [available, downloaded, slot_written, language]
        if available and downloaded and not slot_written:
            # Explicit inherited repair: original C resets despite a failed
            # boot-slot write; typed update keeps the current slot and shows error.
            assert events[0][-2:] == [('slot', 1), ('reset',)]
            expected_tail = [('clear',), ('error', 23)] + ([('clear',)] if language == 1 else [])
            expected_tail += [('failed', headings[language], labels[language])]
            assert events[1] == events[0][:-1] + expected_tail
            repairs.append({'case': case, 'original': events[0], 'recovered': events[1]})
        else:
            assert events[0] == events[1], (case, events)
            records.append(['update', case, events[0]])
    report = {'variant': VARIANT, 'input_sha256': IMAGE_SHA256,
        'elf_sha256': hashlib.sha256(options.elf.read_bytes()).hexdigest(),
        'differential_cases': len(records), 'deliberate_repair_cases': len(repairs),
        'records': records, 'repairs': repairs,
        'limits': ['UI-event rendering is a boundary; localized UI itself has a separate oracle.',
                   'SDK file/radio/reset operations are scripted; no flash-device or appliance validation.']}
    options.output.write_text(json.dumps(report, indent=2) + '\n')
    print('C network:', len(records), 'matching cases;', len(repairs), 'documented reset repairs')


if __name__ == '__main__':
    main()
