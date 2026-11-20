#!/usr/bin/env python3
"""Compare pinned Ghidra instruction exports without asserting source equivalence.

The output is a review aid: instruction shapes erase relocatable addresses, so
equal shapes still require checks of callees, indirect calls, tables and state.
Decompiler function boundaries can merge shared tails or miss entry points.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct

from inspect_oem import IMAGE_BASE, REPO, VARIANTS, read_variant

ROOT = Path(__file__).resolve().parents[1]
SPRO = REPO / 'firmware/pico-s-pro/reverse-engineered'
SPRO_HASH = 'e18c240fa1cf35222674bf3b4fa992a306c6e63b0b99be8793604f199085d702'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load_export(directory, raw):
    manifest_bytes = (directory / 'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest['input']['sha256'] != digest(raw):
        raise ValueError(f'{directory}: input hash mismatch')
    if manifest['analysis_timed_out']:
        raise ValueError(f'{directory}: analysis timed out')

    def checked(relative):
        data = (directory / relative).read_bytes()
        if manifest['files'].get(relative) != digest(data):
            raise ValueError(f'{directory}: changed export file {relative}')
        return json.loads(data)

    functions = checked('functions.json')
    for function in functions:
        function['entry'] = int(function['address'], 16)
        function['instructions'] = checked(function['instructions_file'])
        function['shape'] = instruction_shape(function, raw)
    provenance = {
        'input': manifest['input'],
        'manifest_sha256': digest(manifest_bytes),
        'layout_file_sha256': manifest['layout_file_sha256'],
        'pipeline_sha256': manifest['pipeline_sha256'],
        'tool_archives': manifest['tools']['archives'],
        'functions': len(functions),
    }
    return functions, provenance


def instruction_shape(function, raw):
    instructions = function['instructions']
    local = {int(item['address'], 16): index for index, item in enumerate(instructions)}
    function['literals'] = []
    function['direct_flows'] = []
    result = []
    for item in instructions:
        flows = {int(value, 16) for value in item['flows']}

        def replace(match):
            address = int(match[0], 16)
            if address in local:
                return 'LOCAL:' + str(local[address])
            if IMAGE_BASE <= address < IMAGE_BASE + len(raw):
                if address in flows:
                    function['direct_flows'].append(address)
                    return 'DIRECT_FLOW'
                if address + 4 <= IMAGE_BASE + len(raw):
                    value = struct.unpack_from('<I', raw, address - IMAGE_BASE)[0]
                    function['literals'].append((address, value))
                    if 0x20000000 <= value < 0x20100000:
                        return 'SRAM_POINTER'
                    return 'LITERAL:' + hex(value)
            if 0x20000000 <= address < 0x20100000:
                return 'SRAM_ADDRESS'
            return match[0]

        operands = tuple(re.sub(r'0x[0-9a-fA-F]+', replace, operand)
                         for operand in item['operands'])
        result.append((item['mnemonic'], operands))
    return tuple(result)


def printable_string(raw, address):
    offset = address - IMAGE_BASE
    if not 0 <= offset < len(raw):
        return None
    end = raw.find(b'\0', offset, offset + 1024)
    if end < 0:
        return None
    value = raw[offset:end]
    if len(value) >= 3 and all(32 <= byte < 127 for byte in value):
        return value.decode('ascii')
    return None


def compare(source, target, source_raw, target_raw, names):
    source_shapes, target_shapes = defaultdict(list), defaultdict(list)
    for item in source:
        source_shapes[item['shape']].append(item)
    for item in target:
        target_shapes[item['shape']].append(item)
    source_by = {item['entry']: item for item in source}
    target_by = {item['entry']: item for item in target}
    pairs, reasons = {}, {}
    for shape, items in source_shapes.items():
        candidates = target_shapes.get(shape, [])
        if len(shape) >= 3 and len(items) == len(candidates) == 1:
            pairs[items[0]['entry']] = candidates[0]['entry']
            reasons[items[0]['entry']] = 'unique_instruction_shape'

    # The second pass resolves duplicate tiny functions only when every aligned
    # call site constrains that entry to the same counterpart, in both directions.
    constraints, reverse = defaultdict(set), defaultdict(set)
    for a, b in pairs.items():
        for sa, ta in zip(source_by[a]['direct_flows'], target_by[b]['direct_flows']):
            constraints[sa].add(ta)
            reverse[ta].add(sa)
    different = []
    for a, candidates in sorted(constraints.items()):
        if a in pairs or len(candidates) != 1:
            continue
        b = next(iter(candidates))
        if len(reverse[b]) != 1 or a not in source_by or b not in target_by:
            continue
        if source_by[a]['shape'] == target_by[b]['shape']:
            pairs[a] = b
            reasons[a] = 'instruction_shape_and_unique_call_context'
        else:
            different.append((a, b))

    def identity(a, b):
        original = names.get(a, {})
        return {'source': hex(a), 'target': hex(b),
                'name': original.get('name', source_by[a]['name']),
                'module': original.get('module', 'unknown'),
                'source_instructions': len(source_by[a]['instructions']),
                'target_instructions': len(target_by[b]['instructions'])}

    candidates, conflicts = [], []
    pointers = defaultdict(set)
    equal_strings, changed_strings = 0, []
    for a, b in sorted(pairs.items()):
        left, right = source_by[a], target_by[b]
        if len(left['direct_flows']) != len(right['direct_flows']):
            raise ValueError('Equal shape has inconsistent direct flow count')
        unknown = set()
        for sa, ta in zip(left['direct_flows'], right['direct_flows']):
            if sa in pairs:
                if pairs[sa] != ta:
                    conflicts.append({'caller': hex(a), 'callee': hex(sa),
                                      'expected': hex(pairs[sa]), 'observed': hex(ta)})
            else:
                unknown.add((sa, ta))
        entry = identity(a, b)
        entry['evidence'] = reasons[a]
        entry['unresolved_direct_flows'] = [[hex(sa), hex(ta)] for sa, ta in sorted(unknown)]
        candidates.append(entry)
        for (_, sv), (_, tv) in zip(left['literals'], right['literals']):
            if not (0x20000000 <= sv < 0x20100000 and 0x20000000 <= tv < 0x20100000):
                continue
            pointers[sv].add(tv)
            ss, ts = printable_string(source_raw, sv), printable_string(target_raw, tv)
            if ss is not None and ts is not None:
                if ss == ts:
                    equal_strings += 1
                else:
                    changed_strings.append({'caller': entry['name'], 'source': hex(sv),
                                            'target': hex(tv), 'before': ss, 'after': ts})
    return {
        'counts': {'instruction_shape_candidates': len(candidates),
                   'call_context_structural_differences': len(different),
                   'known_direct_flow_conflicts': len(conflicts),
                   'pointer_correspondences': sum(len(values) for values in pointers.values()),
                   'pointer_conflicts': sum(len(values) > 1 for values in pointers.values()),
                   'equal_string_references': equal_strings,
                   'changed_string_references': len(changed_strings)},
        'candidates_by_module': dict(sorted(Counter(item['module'] for item in candidates).items())),
        'candidates': candidates,
        'different_by_call_context': [identity(a, b) for a, b in different],
        'direct_flow_conflicts': conflicts,
        'pointer_correspondences': {hex(key): [hex(v) for v in sorted(values)]
                                    for key, values in sorted(pointers.items())},
        'changed_string_references': changed_strings,
        'unmatched_target_entries': [hex(item['entry']) for item in target
                                     if item['entry'] not in set(pairs.values())],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spro-export', required=True, type=Path)
    parser.add_argument('--main-export', required=True, type=Path)
    parser.add_argument('--alt-export', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'analysis/function-correspondence.json')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    original = (SPRO / 'reference/original-0.1.34.bin').read_bytes()
    if digest(original) != SPRO_HASH:
        raise ValueError('S/Pro reference image hash mismatch')
    names = {int(item['address'], 16): item for item in json.loads(
        (SPRO / 'reference/function_map.json').read_text())}
    source, source_meta = load_export(args.spro_export, original)
    report = {'schema_version': 1, 'source_equivalence_claimed': False,
              'hardware_tested': False, 'reference': source_meta,
              'limits': [
                  'Equal instruction shapes are review candidates, not behavioral verification.',
                  'Pointer values and direct flow addresses are normalized; referenced data, indirect calls and callee semantics require separate review.',
                  'Ghidra recognized ranges can include shared tail bodies and omit unrecognized entries.',
                  'Call-context differences identify candidates for manual review; names are inherited hypotheses.',
              ], 'variants': {}}
    for variant, directory in zip(VARIANTS, (args.main_export, args.alt_export)):
        raw = read_variant(variant)
        target, metadata = load_export(directory, raw)
        report['variants'][VARIANTS[variant]['display']] = {
            'provenance': metadata, 'comparison': compare(source, target, original, raw, names)}
    encoded = json.dumps(report, indent=2, sort_keys=False) + '\n'
    if args.check:
        if args.output.read_text() != encoded:
            raise ValueError('Correspondence report differs from checked exports')
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    for variant, result in report['variants'].items():
        print(variant, json.dumps(result['comparison']['counts'], sort_keys=True))


if __name__ == '__main__':
    main()
