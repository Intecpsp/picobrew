"""Explicit C entries/data for the archived network-test vocabulary.

The emulator never remaps a program counter. Callers select the actual C
address before invoking the original instructions. Unmapped values fail.
"""
import json
from pathlib import Path
from original_arm import VARIANT
ROOT = Path(__file__).resolve().parents[1]
comparison = json.loads((ROOT / 'analysis/function-correspondence.json').read_text())[
    'variants'][VARIANT.upper()]['comparison']
functions = comparison['candidates'] + comparison['different_by_call_context']
MAP = {entry['name']: int(entry['target'], 16) for entry in functions}
ADDRESSES = {int(entry['source'], 16): int(entry['target'], 16) for entry in functions}
ADDRESSES.update({int(source, 16): int(targets[0], 16)
                  for source, targets in comparison['pointer_correspondences'].items()
                  if len(targets) == 1})
# C byte evidence beyond the normalized correspondence report:
# - millis entry is 17d40 / 17c9c.
# - IPAddress vtable is loaded by main18330 from literal18344 (alt182a0).
# - security is offset61 from credentials base bcc4, loaded by08ef8.
# - server IP is +4 from b6a0/912c literal (c404).
# - SDK zero IP is +4 from b69c literal (35400).
ADDRESSES.update({0x20018efc: 0x20017c9c if VARIANT == 'ssd1309' else 0x20017d40,
                  0x200294dc: 0x20027f68 if VARIANT == 'ssd1309' else 0x20028008,
                  0x2002c2f1: 0x2002bd25,
                  0x2002dacc: 0x2002c408,
                  0x20036ac8: 0x20035404})
MAP['millis'] = ADDRESSES[0x20018efc]
MAP['pico_ui_draw_registration_code'] = 0x20007a30 - (24 if VARIANT == 'ssd1309' else 0)
for name, main in {'pico_api_register_device':0x2000f5e0,
                   'pico_api_has_cleaned_ack':0x2000f7b4,
                   'pico_api_set_cleaned_ack':0x2000f7c0}.items():
    MAP[name] = main - (24 if VARIANT == 'ssd1309' else 0)

def address(reference_address):
    return ADDRESSES[reference_address]
