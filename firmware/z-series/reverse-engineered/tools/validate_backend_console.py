#!/usr/bin/env python3
"""Compare complete backend diagnostic callbacks and the Core test marker."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from backend_console_cases import cases
from backend_console_native import load_native, run, SOURCES

GUARDS = {
    'log|3|ZBACKEND|ZBackend.h|449|valvePosition1 = 100.000000':
        'log|3|ZBACKEND|ZBackend.h|449|valvePosition1 = 1',
    'log|3|ZBACKEND|ZBackend.h|494|valvePosition2 = 50.099976':
        'log|3|ZBACKEND|ZBackend.h|494|valvePosition2 = 1',
}


def guarded_original(case, original):
    """Project only the two proven integer-as-double vararg log defects."""
    if case['operation'] != 1:
        return original
    events = original['events']
    if any(events.count(key) != 1 for key in GUARDS):
        raise ValueError('Malformed-vararg characterization changed')
    return dict(original, events=[GUARDS.get(event, event) for event in events])


def verify(directory):
    inputs = json.loads(gzip.decompress((ROOT / 'tests/fixtures/backend-console-inputs.json.gz').read_bytes()))
    original = json.loads(gzip.decompress((ROOT / 'tests/fixtures/backend-console-original.json.gz').read_bytes()))
    if inputs != cases() or len(inputs) != len(original['cases']):
        raise ValueError('Original input identity/count differs')
    lib = load_native(directory)
    exact = 0
    for index, (case, expected) in enumerate(zip(inputs, original['cases'])):
        actual = run(lib, case)
        exact += actual == expected
        if actual != guarded_original(case, expected):
            raise ValueError(f'Backend diagnostic differs outside documented guard in case {index}')
    if lib.backend_console_service_failures() != 255:
        raise ValueError('A required or failed callback was represented as success')
    paths = SOURCES + [
        'src/backend_console_documents.inc', 'include/zseries/backend_console_tests.hpp',
        'include/zseries/application_console.hpp',
        'include/zseries/backend_log_event.hpp', 'include/zseries/console_test_support.hpp',
        'include/zseries/program_diagnostics.hpp', 'include/zseries/http_backend.hpp',
        'include/zseries/program_recipe.hpp', 'include/zseries/program_repository.hpp',
        'tests/backend_console_cases.py', 'tests/backend_console_native.py',
        'tests/test_backend_console.py', 'tests/run_backend_console_emulation.py',
        'tests/ghidra/EmulateBackendConsoleTests.java', 'tests/ghidra/BackendConsoleExecution.java',
        'tools/validate_backend_console.py', 'tools/build_backend_console_objects.py',
        'tests/fixtures/backend-console-inputs.json.gz', 'tests/fixtures/backend-console-original.json.gz',
    ]
    return dict(
        schema_version=1, input_sha256=original['input_sha256'], cases=len(inputs),
        original_functions=len(original['original_functions']),
        returning_assertions=original['original_functions']['0x400db014'],
        exact_cases=exact, guarded_cases=len(inputs)-exact,
        all_results_match_outside_documented_guards=True,
        documented_guards=[dict(original=key, reconstructed=value,
            reason='The OEM diagnostic passes int ValvePosition to %f, which reads an unrelated ABI word. The reconstruction prints the checked integer with %d; all assertions, state and timing remain compared.')
            for key, value in GUARDS.items()],
        source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths},
        scope='Complete original HttpBackend 401188f0 and ZBackend 4011dc20 diagnostic suites, Core test marker 400ee670/4010a26c, and log-event encoders 4011da68/4011d974 execute through return. Every diagnostic, scope, marker output, state, wrapping counter and clock is compared. The real assertion helper 400db014 increments the counter, delays 200 ms and returns. Production JSON parsers, program writer, full instruction/statistics reports and session serializer are exercised. Mapped image segments are byte-verified. ROM memory/strings, scalar math, reviewed conversion/clear loops, bounded formatting, SDK clock/stack/watchdog, C++ guards, diagnostic rendering and the separately reconstructed Head marker renderer are explicit boundaries. The selected ArduinoJson 6.21.6 is pinned; its exact OEM minor revision is unknown. Existing guarded malformed-recipe policies are not changed or claimed as exact by these successful self-test fixtures.',
        modeled_boundaries=original['modeled_boundaries'], hardware_tested=False,
        complete_application_linked=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-report', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='backend-console-') as directory:
        report = verify(directory)
    if args.write_report:
        (ROOT / 'analysis/backend-console-validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['cases', 'original_functions',
        'returning_assertions', 'exact_cases', 'guarded_cases',
        'all_results_match_outside_documented_guards']}, indent=2))


if __name__ == '__main__':
    main()
