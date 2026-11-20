#!/usr/bin/env python3
"""Reproduce the backend comparisons, including explicit OEM memory-failure differences."""
import argparse
from collections import Counter
import copy
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from validate_display import IMAGE_SHA, digest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from backend_services_cases import json_cases, lifecycle_cases
from program_recipe_native import dependency

SOURCES = [
    'src/z_backend.cpp', 'adapters/arduinojson/z_backend_json.cpp',
    'src/http_backend.cpp', 'adapters/arduinojson/http_backend_json.cpp',
    'src/azure.cpp', 'adapters/arduinojson/azure_json.cpp', 'src/backend_console.cpp',
    'src/quantity.cpp', 'src/wifi.cpp', 'src/console.cpp', 'src/local_http_server.cpp',
    'src/program_list.cpp', 'src/program_recipe.cpp', 'src/program_repository.cpp',
    'src/program_catalog.cpp', 'src/program_diagnostics.cpp', 'src/program_writer.cpp', 'src/runtime_instruction.cpp',
    'src/program_runtime_execution.cpp', 'src/program_runtime.cpp', 'src/enums.cpp',
]
OWNED = [
    'include/zseries/z_backend.hpp', 'include/zseries/http_backend.hpp',
    'include/zseries/azure.hpp', 'include/zseries/backend_console.hpp',
    'src/z_backend.cpp', 'adapters/arduinojson/z_backend_json.cpp',
    'src/http_backend.cpp', 'adapters/arduinojson/http_backend_json.cpp',
    'src/azure.cpp', 'adapters/arduinojson/azure_json.cpp', 'src/backend_console.cpp',
    'platform/esp32/azure_crypto.hpp', 'platform/esp32/azure_crypto.cpp',
    'tests/backend_services_probe.cpp', 'tests/backend_services_cases.py',
    'tools/validate_backend_services.py', 'tools/ghidra/EmulateBackendJson.java',
    'tools/ghidra/EmulateAzure.java', 'tools/ghidra/Z119Execution.java',
    'dependencies/arduinojson.json',
]
DIVERGENT = {'backend-59', 'edge-2'}
TERMINAL = 'SDK_abort_after_stack_canary_corruption'


def build(directory):
    directory = Path(directory)
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    msp = directory / 'msp.o'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-I', str(ROOT / 'include'),
        '-c', str(ROOT / 'src/msp_protocol.c'), '-o', str(msp)], check=True)
    binary = directory / 'backend-services'
    subprocess.run(compiler + ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fno-fast-math', '-ffp-contract=off', '-ffunction-sections', '-Wl,--gc-sections',
        '-I', str(ROOT / 'include'), '-I', str(dependency() / 'src'),
        str(ROOT / 'tests/backend_services_probe.cpp')] + [str(ROOT / p) for p in SOURCES] +
        [str(msp), '-o', str(binary)], check=True)
    return binary, subprocess.check_output(compiler + ['--version'], text=True).splitlines()[0]


def run(binary, cases):
    output = subprocess.run([str(binary)], input=''.join(json.dumps(case) + '\n' for case in cases),
                            text=True, capture_output=True, check=True)
    results = [json.loads(line) for line in output.stdout.splitlines()]
    if len(results) != len(cases):
        raise ValueError('Incomplete native probe output')
    return results


def expected(case, original):
    value = copy.deepcopy(original)
    if case['id'] in DIVERGENT:
        if case['op'] != 'z_endpoint' or case['session'] < 1000000000 or value.get('terminal') != TERMINAL:
            raise ValueError('Memory-failure characterization no longer describes this original run')
        del value['terminal']
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-input', type=Path)
    parser.add_argument('--original-directory', type=Path)
    parser.add_argument('--export', type=Path, action='append', default=[])
    parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args()
    groups = {'json': json_cases(), 'azure': lifecycle_cases()}
    if args.prepare_input:
        args.prepare_input.mkdir(parents=True, exist_ok=True)
        for name, cases in groups.items():
            (args.prepare_input / ('final-' + name + '-cases.json')).write_text(
                json.dumps(cases, separators=(',', ':')) + '\n')
        print('Prepared backend inputs:', {name: len(value) for name, value in groups.items()})
    if not args.original_directory:
        return
    if digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin') != IMAGE_SHA:
        raise ValueError('Original image identity differs')
    cases, originals, executions = [], [], {}
    for name, group in groups.items():
        original_file = args.original_directory / ('final-' + name + '-original.jsonl')
        execution = json.loads(Path(str(original_file) + '.json').read_text())
        serialized = (json.dumps(group, separators=(',', ':')) + '\n').encode()
        if execution['input_sha256'] != IMAGE_SHA or execution['input_file_sha256'] != hashlib.sha256(serialized).hexdigest():
            raise ValueError('Original input identity differs: ' + name)
        rows = [json.loads(line) for line in original_file.read_text().splitlines()]
        if len(rows) != len(group) or execution['cases'] != len(group):
            raise ValueError('Incomplete original execution: ' + name)
        cases += group
        originals += rows
        executions[name] = dict(execution, execution_jsonl_sha256=digest(original_file))
    with tempfile.TemporaryDirectory(prefix='z-backend-services-') as directory:
        binary, compiler = build(directory)
        actual = run(binary, cases)
    for case, original, native in zip(cases, originals, actual):
        if expected(case, original) != native:
            fields = [key for key in set(original) | set(native) if original.get(key) != native.get(key)]
            raise ValueError('Backend mismatch: ' + case['id'] + ' fields=' + repr(fields))
    if args.write_evidence:
        if not args.export:
            parser.error('--export is required for original instruction hashes')
        output = io.BytesIO()
        with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0) as compressed:
            compressed.write((json.dumps(dict(input_sha256=IMAGE_SHA,
                cases=[dict(input=i, original=o) for i, o in zip(cases, originals)]), separators=(',', ':')) + '\n').encode())
        fixture = ROOT / 'tests/fixtures/backend-services-original.json.gz'
        fixture.write_bytes(output.getvalue())
        functions, boundaries = Counter(), Counter()
        for execution in executions.values():
            functions.update(execution['original_functions_executed'])
            boundaries.update(execution['modeled_boundaries'])
        instruction_hashes = {}
        for address in functions:
            for export in args.export:
                path = export / 'instructions' / ('fn_' + address[2:] + '.json')
                if path.is_file():
                    instruction_hashes[address] = digest(path)
                    break
            else:
                raise ValueError('No instruction export for ' + address)
        report = dict(schema_version=1, input_sha256=IMAGE_SHA, cases=len(cases),
            exact_matches=len(cases)-len(DIVERGENT), all_results_match=False,
            all_documented_results_match=True, hardware_tested=False, complete_application_linked=False,
            compiler=compiler, source_sha256={p: digest(ROOT / p) for p in OWNED},
            golden_fixture_sha256=digest(fixture), executions=executions,
            original_functions_executed=dict(functions), modeled_boundaries=dict(boundaries),
            original_instruction_sha256=instruction_hashes,
            bounded_memory_divergences=[dict(case_id=i, original_terminal=TERMINAL,
                native_behavior='Return with the bounded URI; a 16-byte decimal buffer preserves the stack canary.')
                for i in sorted(DIVERGENT)],
            scope='Actual original HttpBackend tick/request/registration/list/reference bodies; ZBackend session/log JSON and completion; Azure lifecycle, command/session parser, SAS construction, console registration. Returning diagnostic helper is executed including counter increment and 200ms delay. HTTP/MQTT, HMAC/base64, ROM/compiler operations, Core mailboxes and Shell registration are explicit boundaries. Full program builders retain separately documented compatibility limits.')
        (ROOT / 'analysis/backend-services-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Validated {len(cases)} backend cases: {len(cases)-len(DIVERGENT)} exact, {len(DIVERGENT)} documented OEM memory-failure differences')


if __name__ == '__main__':
    main()
