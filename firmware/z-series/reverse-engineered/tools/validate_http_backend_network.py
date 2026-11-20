#!/usr/bin/env python3
"""Compare the complete original HttpBackend network diagnostic through return."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from http_backend_network_cases import cases
from http_backend_network_native import load_native, run, digest, SOURCES


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
        'tests/http_backend_network_cases.py', 'tests/http_backend_network_native.py',
        'tests/backend_console_native.py', 'tests/program_recipe_native.py',
        'tests/test_http_backend_network.py', 'tests/run_http_backend_network_emulation.py',
        'tests/run_http_backend_network_sanitizer.py',
        'tests/ghidra/BackendConsoleExecution.java', 'tests/ghidra/EmulateHttpBackendNetwork.java',
        'tools/validate_http_backend_network.py', 'tools/build_http_backend_network_objects.py',
        'tools/recover_backend_credentials.py', 'dependencies/arduinojson.json',
        'tests/fixtures/http-backend-network-inputs.json.gz',
        'tests/fixtures/http-backend-network-original.json.gz',
        'analysis/http-backend-network-execution-tools.json',
    ])
    return sorted(paths)


def contracts(native, case):
    total = 0
    for missing, failure in [(value, 0) for value in range(1, 9)]+[(0, value) for value in range(1, 7)]:
        actual = run(native, case, missing, failure)
        if actual['result'] or actual['identifier_sha256'] != digest(case['uid']):
            raise ValueError('Failed service was accepted or temporary identifier was retained')
        total += 1
    return total


def verify(directory):
    input_bytes = gzip.decompress((ROOT / 'tests/fixtures/http-backend-network-inputs.json.gz').read_bytes())
    inputs = json.loads(input_bytes)
    original = json.loads(gzip.decompress((ROOT / 'tests/fixtures/http-backend-network-original.json.gz').read_bytes()))
    if inputs != cases() or len(inputs) != len(original['cases']) or not original['secret_payloads_hashed']:
        raise ValueError('Original input identity/count or private-trace policy differs')
    if hashlib.sha256(input_bytes).hexdigest() != original['input_file_sha256']:
        raise ValueError('The original replay used different input bytes')
    execution = json.loads((ROOT / 'analysis/http-backend-network-execution-tools.json').read_text())
    for name, expected in execution['script_sha256'].items():
        if hashlib.sha256((ROOT / 'tests/ghidra' / name).read_bytes()).hexdigest() != expected:
            raise ValueError('An original execution script changed without a fresh replay')
    native = load_native(directory)
    for index, (case, expected) in enumerate(zip(inputs, original['cases'])):
        if run(native, case) != expected:
            raise ValueError('Full backend HTTP comparison differs in case '+str(index))
    count = contracts(native, inputs[0])
    return dict(schema_version=1, input_sha256=original['input_sha256'], cases=len(inputs),
        original_functions=len(original['original_functions']),
        returning_assertions=original['original_functions']['0x400db014'],
        ordered_events=sum(len(row['events']) for row in original['cases']),
        required_and_failed_service_contracts=count, all_results_match=True,
        source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in source_inputs()},
        modeled_boundaries=original['modeled_boundaries'], secret_payloads_hashed=True,
        scope='Complete original HttpBackend diagnostic 4011b6cc executes all nine requests through return, including actual URI/configure/send/HTTP tick/receiver/parser/program-builder/report bodies, TestScope and real returning assertions. Twelve prescribed response cohorts compare all 24 final state words, restored identifier digest and ordered event traces. The original state-6 test calls the PicoPak parser; this behavior is retained. Clock/counter wrapping, low stack, invalid JSON, wrong values, client/connect failures, content-type/status/length errors, empty original UID and same-as-test UID are exercised. Socket allocation/connect/read/write, SDK clocks/heap/yield/watchdog, ROM memory/strings, scalar math, bounded formatting, C++ guards, scope rendering and the queue handoff are explicit boundaries. Request writes, bulk response-body reads, host strings and formatted messages are hashed before fixture output; original protocol resources are generated only inside ignored build output. No assertion boundary aborts the test. Fourteen native missing/failed-service cases must fail and restore the temporary identifier. The selected ArduinoJson version is pinned; its exact OEM minor revision is unknown. Existing guarded malformed-recipe policies are not claimed as exact by these fixtures.',
        hardware_tested=False, live_network_tested=False, complete_application_linked=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-report', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='http-backend-network-') as directory:
        report = verify(directory)
    if args.write_report:
        (ROOT / 'analysis/http-backend-network-validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['cases', 'original_functions', 'returning_assertions',
        'ordered_events', 'required_and_failed_service_contracts', 'all_results_match']}, indent=2))


if __name__ == '__main__':
    main()
