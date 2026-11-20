#!/usr/bin/env python3
"""Compare the recovered Basic diagnostic leaves and full aggregate with Z119."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from basic_console_cases import cases, LEAVES
from basic_console_native import load_native, run, SOURCES
from validate_backend_console import guarded_original, GUARDS
from extract_basic_console_data import outputs


def expected_result(case, original):
    if case['callback'] not in ('4011ed44', '4011fbac'):
        return original
    return guarded_original({'operation': 1}, original)


def source_inputs():
    paths = set(SOURCES)
    pending = list(SOURCES)
    while pending:
        path = ROOT / pending.pop()
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(), re.MULTILINE):
            candidates = [path.parent / include, ROOT / 'include' / include]
            candidate = next((item.resolve() for item in candidates if item.is_file()), None)
            if candidate is not None and ROOT in candidate.parents:
                relative = candidate.relative_to(ROOT).as_posix()
                if relative not in paths:
                    paths.add(relative)
                    pending.append(relative)
    paths.update([
        'tests/basic_console_cases.py', 'tests/basic_console_native.py',
        'tests/test_basic_console.py', 'tests/run_basic_console_emulation.py',
        'tests/ghidra/BasicConsoleExecution.java', 'tests/ghidra/EmulateBasicConsoleTests.java',
        'tools/validate_basic_console.py', 'tools/validate_backend_console.py',
        'tools/build_basic_console_objects.py', 'tools/extract_basic_console_data.py',
        'tests/fixtures/basic-console-inputs.json.gz', 'tests/fixtures/basic-console-original.json.gz',
        'analysis/basic-console-data.json', 'analysis/basic-console-execution-tools.json',
        'dependencies/arduinojson.json',
    ])
    return sorted(paths)


def verify(directory):
    inputs = json.loads(gzip.decompress((ROOT / 'tests/fixtures/basic-console-inputs.json.gz').read_bytes()))
    original = json.loads(gzip.decompress((ROOT / 'tests/fixtures/basic-console-original.json.gz').read_bytes()))
    if inputs != cases() or len(inputs) != len(original['cases']) or any('error' in row for row in original['cases']):
        raise ValueError('Original Basic inputs/count/completion differ')
    for path, data in outputs().items():
        if (ROOT / path).read_bytes() != data:
            raise ValueError('Original Basic embedded data differs: ' + path)
    native = load_native(directory)
    exact = 0
    for index, (case, expected) in enumerate(zip(inputs, original['cases'])):
        actual = run(native, case)
        exact += actual == expected
        if actual != expected_result(case, expected):
            raise ValueError(f'Basic diagnostic differs outside documented guards in case {index}')
    if native.basic_console_service_failures() != 255:
        raise ValueError('Missing or failed diagnostic service was reported as success')
    return dict(
        schema_version=1, input_sha256=original['input_sha256'], cases=len(inputs),
        unique_leaf_callbacks=len(LEAVES), aggregate_callbacks=['0x4011ed44', '0x4011fbac'],
        original_functions=len(original['original_functions']),
        returning_assertions=original['original_functions']['0x400db014'],
        exact_cases=exact, guarded_cases=len(inputs) - exact,
        all_results_match_outside_documented_guards=True,
        documented_guards=[dict(original=key, reconstructed=value,
            reason='The shared ZBackend self-test passes an integer ValvePosition to %f in the OEM. The reconstruction prints the verified integer; only these two log strings change.')
            for key, value in GUARDS.items()],
        source_sha256={path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in source_inputs()},
        modeled_boundaries=original['modeled_boundaries'], hardware_tested=False, complete_application_linked=False,
        scope='All 37 recovered Basic leaf callbacks and the Core::basicTests aggregate/wrapper execute through return in the original image. Four clock/stack/counter states cover the stack threshold, zero-clock sentinel, integer wrapping and returning assertion failures. The real helper 400db014 logs, increments the counter, delays 200 ms and returns. The aggregate executes all reused EEPROM/MAX/Runtime/Still/backend bodies and both test-marker bodies; the separately reconstructed Head test dispatch 400fe350 and display-message renderer are explicit boundaries. Every remaining ordered scope/diagnostic/raw serial event, assertion counter and clock is compared. Reviewed ROM memory/string routines, scalar math, fixed formatting, SDK time/stack/watchdog/output, heap allocation success, C++ guards and instruction-adaptation rules are explicit environment boundaries. Mapped bytes are checked against the preserved OEM image. The selected ArduinoJson version is pinned but its exact OEM minor revision is unknown. These diagnostic fixtures do not establish malformed production-input, live-network, RTOS, peripheral or hardware parity.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-report', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='basic-console-') as directory:
        report = verify(directory)
    if args.write_report:
        (ROOT / 'analysis/basic-console-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ['cases', 'unique_leaf_callbacks', 'original_functions',
        'returning_assertions', 'exact_cases', 'guarded_cases', 'all_results_match_outside_documented_guards']}, indent=2))


if __name__ == '__main__':
    main()
