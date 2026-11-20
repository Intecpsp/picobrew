"""Explicit C-variant address selection for original-instruction tests.

This file never translates an emulated PC implicitly. Tests call these helpers
when selecting a real entry; production source contains no original addresses.
"""
import json
from pathlib import Path

from original_arm import VARIANT

ROOT = Path(__file__).resolve().parents[1]
_report = json.loads((ROOT / 'analysis/function-correspondence.json').read_text())


def _entries(display):
    comparison = _report['variants'][display]['comparison']
    return comparison['candidates'] + comparison['different_by_call_context']


_display = VARIANT.upper()
_selected = _entries(_display)
_by_source = {item['source']: int(item['target'], 16) for item in _selected}
_by_name = {item['name']: int(item['target'], 16) for item in _selected}
_from_main = {int(item['target'], 16): _by_source[item['source']]
              for item in _entries('SSD1325') if item['source'] in _by_source}
_main_data = _report['variants']['SSD1325']['comparison']['pointer_correspondences']
_selected_data = _report['variants'][_display]['comparison']['pointer_correspondences']
_data_from_main = {int(values[0], 16): int(_selected_data[source][0], 16)
                   for source, values in _main_data.items()
                   if len(values) == 1 and source in _selected_data
                   and len(_selected_data[source]) == 1}
for _entry in json.loads((ROOT / 'analysis/manual-entry-points.json').read_text())['functions']:
    _by_name[_entry['name']] = int(_entry[VARIANT], 16)
    _from_main[int(_entry['ssd1325'], 16)] = int(_entry[VARIANT], 16)


def named_address(name):
    return _by_name[name]


def variant_address(main_address):
    if VARIANT == 'ssd1325':
        return main_address
    return _from_main[main_address]


def data_address(main_address):
    if VARIANT == 'ssd1325':
        return main_address
    return _data_from_main[main_address]
