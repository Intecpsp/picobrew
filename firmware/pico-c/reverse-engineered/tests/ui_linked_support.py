"""Compare C OEM routines with the separately linked native application image.

Only named I/O or subordinate service boundaries are intercepted. The rebuilt
entry runs ARM instructions from the complete ELF, never from the OEM image.
Compiler DWARF supplies every typed-object offset used by the harness.
"""
from pathlib import Path
import json
import struct
import sys
from test_control import OriginalC

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT.parents[1] / 'pico-s-pro/reverse-engineered'
sys.path.insert(0, str(REFERENCE / 'tools'))
sys.path.insert(0, str(REFERENCE / 'tests'))
from fast_arm import RebuiltArm
from test_native_startup import Layout


def argument(arm, index):
    return arm.reg(index) if index < 4 else arm.read_u32(arm.reg('sp') + (index - 4) * 4)


class Pair:
    def __init__(self, variant, elf, layout, language=0):
        self.variant, self.layout = variant, layout
        self.old, self.new = OriginalC(variant), RebuiltArm(elf)
        self.new.hook_symbol('millis', lambda r: 0)
        self.new.hook_symbol('__aeabi_atexit', lambda r: 0)
        self.new.call_symbol('_GLOBAL__sub_I_setup', max_instructions=250000)
        self.application = self.new.address('_ZL11application')
        self.objects = {name: self.application + layout.offset('PicoApplication', name)
                        for name in ('state', 'settings', 'network', 'ui', 'device', 'phase', 'recipe')}
        self.ui, self.state, self.net = (self.objects[k] for k in ('ui', 'state', 'network'))
        mapping = json.loads((ROOT / 'analysis/function-correspondence.json').read_text())
        candidate = mapping['variants']['SSD1309' if variant == 'ssd1309' else 'SSD1325']['comparison']
        self.addresses = {e['name']: int(e['target'], 16)
                          for e in candidate['candidates'] + candidate['different_by_call_context']}
        main = mapping['variants']['SSD1325']['comparison']
        self.main_to_variant = {int(e['target'],16): self.addresses[e['name']]
                                for e in main['candidates'] + main['different_by_call_context']
                                if e['name'] in self.addresses}
        self.events = {'old': [], 'new': []}
        self.tables = {}
        self.traps = 0
        self.old.write(0x2002a90c, bytes([language]))
        self.write('settings', 'PicoSettings', 'language', language, 'B')
        self.old.hook(self.addresses['pico_log_serial'], lambda r: None)
        self.old.hook(self.addresses['pico_assert_nonfatal'], lambda r: None)
        self.new.hook_symbol('pico_network_log', lambda r: None)
        self.new.hook_symbol('pico_network_assert', lambda r: None)

    def entry(self, main):
        if self.variant == 'ssd1325':
            return main
        if main in self.main_to_variant:
            return self.main_to_variant[main]
        if main == 0x20012f64:  # ROM-bitmap overload, verified in both rasterizers.
            return 0x20012edc
        if main >= 0x20017b14:
            return main - 0xa4
        if main >= 0x20005500:
            return main - 0x18
        if main >= 0x20005400:
            return main - 0x14
        return main + 4

    def write(self, obj, record, member, value, kind='I'):
        address = self.objects[obj] + self.layout.offset(record, member)
        self.new.write(address, value if isinstance(value, bytes) else struct.pack('<' + kind, value))

    def event(self, side, name, *values):
        if len(self.events[side]) >= 4096:
            raise AssertionError(('Unbounded trace', side, self.events[side][:16]))
        self.events[side].append([name, *values])

    def hook(self, name, original=None, count=0, old_shift=0, new_shift=0, returns=0,
             sequence=None, strings=()):
        address = self.addresses[original or name] if not isinstance(original, int) else self.entry(original)
        indices = {'old': 0, 'new': 0}
        for side, arm in [('old', self.old), ('new', self.new)]:
            shift = old_shift if side == 'old' else new_shift
            def callback(r, side=side, shift=shift):
                values = [argument(r, i + shift) for i in range(count)]
                for i in strings:
                    values[i] = r.cstring(values[i]).hex()
                result = returns
                if sequence is not None:
                    index = indices[side]
                    if index >= len(sequence):
                        raise AssertionError((name, 'unexpected boundary call', side, index))
                    result = sequence[index]
                    indices[side] += 1
                self.event(side, name, *values)
                return result
            arm.hook(address if side == 'old' else self.new.address(name), callback)
        return indices

    def callback(self, record, field, handler):
        if record not in self.tables:
            owner, owner_record, member = {
                'PicoUiServices': (self.ui, 'PicoUi', 'services'),
                'PicoUiHardware': (self.ui, 'PicoUi', 'hardware'),
                'PicoNetworkPlatform': (self.net, 'PicoNetwork', 'platform'),
            }[record]
            location = owner + self.layout.offset(owner_record, member)
            old_table = self.new.read_u32(location)
            new_table = self.new.SCRATCH + 0x1000 + len(self.tables) * 0x400
            self.new.write(new_table, self.new.read(old_table, self.layout.size(record)))
            self.new.write_u32(location, new_table)
            self.tables[record] = new_table
        trap = self.new.SCRATCH + 0x4000 + self.traps * 4
        self.traps += 1
        self.new.write(trap, b'\x70\x47')
        self.new.hook(trap, handler)
        self.new.write_u32(self.tables[record] + self.layout.offset(record, field), trap | 1)

    def service(self, field, original, count=0, returns=0):
        address = self.addresses[original] if isinstance(original, str) else self.entry(original)
        def run(r, side, shift):
            self.event(side, field, *[argument(r, shift + i) for i in range(count)])
            return returns
        self.old.hook(address, lambda r: run(r, 'old', 0))
        self.callback('PicoUiServices', field, lambda r: run(r, 'new', 1))

    def clock(self, step=401, start=0):
        counts = {'old': 0, 'new': 0}
        def now(side):
            value = (start + step * counts[side]) & 0xffffffff
            counts[side] += 1
            self.event(side, 'millis', value)
            return value
        self.old.hook(self.entry(0x20017d40), lambda r: now('old'))
        self.callback('PicoUiHardware', 'millis', lambda r: now('new'))

    def clicks(self, count):
        self.hook('pico_encoder_take_click', count=0, sequence=[0] * (count - 1) + [1])

    def drawing(self):
        for name, address, count, strings in [
            ('pico_display_clear', 0x20004488, 0, ()),
            ('pico_display_flush', 0x20004494, 0, ()),
            ('pico_display_center_text', 0x200054ac, 2, (1,)),
            ('pico_display_text_at_cell', 0x20005474, 3, (2,)),
            ('pico_display_clear_row', 0x20009944, 1, ()),
            ('pico_display_builtin_font', 0x200053f0, 0, ()),
            ('pico_display_custom_font', 0x200053fc, 0, ()),
            ('pico_display_update_timer', 0x20009ee4, 3, ()),
            ('pico_display_wait_for_click', 0x20009a54, 0, ()),
            ('pico_ui_wait_for_continue', 0x2000e394, 0, ()),
            ('pico_ui_wait_for_stop', 0x2000e6b0, 0, ()),
            ('pico_ui_draw_footer_single', 0x2000b080, 1, (0,)),
        ]:
            # Flush and clear have the same address in both original variants.
            if address in (0x20004488, 0x20004494):
                self.addresses[name] = address
                self.hook(name, count=count, strings=strings)
            else:
                self.hook(name, address, count=count, strings=strings)

    def invoke(self, name, entry, args=(), compare_return=False, new_args=None, stack=(), new_stack=None):
        old = self.old.call(self.entry(entry), list(args), stack_args=stack, max_instructions=1000000)
        new = self.new.call_symbol(name, list(args if new_args is None else new_args),
                                  stack_args=stack if new_stack is None else new_stack,
                                  max_instructions=1000000)
        if self.events['old'] != self.events['new']:
            a, b = self.events['old'], self.events['new']
            index = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
            raise AssertionError((self.variant, name, 'event', index, 'old', a[max(0,index-3):index+6], 'new', b[max(0,index-3):index+6], 'counts', len(a), len(b)))
        if compare_return and old['r0'] != new['r0']:
            raise AssertionError((name, 'return', old['r0'], new['r0']))
        return self.events['old']
