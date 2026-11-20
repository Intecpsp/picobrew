#!/usr/bin/env python3
"""Compare complete Core diagnostic orchestration and BinaryDownload<16>."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from network_console_cases import cases
from network_console_native import load_native, run, SOURCES


def verify(directory):
    inputs = json.loads(gzip.decompress((ROOT / 'tests/fixtures/network-console-inputs.json.gz').read_bytes()))
    original = json.loads(gzip.decompress((ROOT / 'tests/fixtures/network-console-original.json.gz').read_bytes()))
    if inputs != cases() or len(inputs) != len(original['cases']):
        raise ValueError('Original input identity/count differs')
    native = load_native(directory)
    for index, (case, expected) in enumerate(zip(inputs, original['cases'])):
        if run(native, case) != expected:
            raise ValueError(f'Network orchestration differs in case {index}')
    if native.network_console_failures() != 255:
        raise ValueError('A missing or failed service was represented as success')
    paths = SOURCES + [
        'include/zseries/network_console_tests.hpp', 'include/zseries/console_test_support.hpp',
        'include/zseries/application_console.hpp',
        'tests/network_console_cases.py', 'tests/network_console_native.py',
        'tests/test_network_console.py', 'tests/run_network_console_emulation.py',
        'tests/ghidra/EmulateNetworkConsoleTests.java', 'tests/ghidra/BackendConsoleExecution.java',
        'tools/validate_network_console.py', 'tools/build_network_console_objects.py',
        'tests/fixtures/network-console-inputs.json.gz', 'tests/fixtures/network-console-original.json.gz',
    ]
    return dict(schema_version=1, input_sha256=original['input_sha256'], cases=len(inputs),
        original_functions=len(original['original_functions']),
        returning_assertions=original['original_functions']['0x400db014'],
        all_results_match=True, required_service_contracts=8,
        source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths},
        scope='The original BinaryDownload<16> self-test 401024a8, Network 4011bffc/4011c2f8, Int 4010a364/4010a3d8 and All 4011fbbc run through return. The actual test marker and returning assertion helper execute; all ordered diagnostics, scope/marker calls, counter wrapping, time and network selection match. Child diagnostic bodies in the Network/Int/All orchestration are explicit typed service boundaries: each call has prescribed elapsed time and returning assertions. These cases prove orchestration and callback failure propagation, not a monolithic execution of every nested child. Separate leaf proofs establish the child bodies. BinaryDownload executes its original 16-byte specialization, two eight-byte writes and completion. Its original uninitialized local payload is unobserved; the reconstruction uses zero bytes while preserving counts, copies and completion. The actual network factory defined return paths make its return value and output-client presence coincide. No live network service or hardware is contacted.',
        modeled_boundaries=original['modeled_boundaries'], hardware_tested=False,
        complete_application_linked=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-report', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='network-console-') as directory:
        report = verify(directory)
    if args.write_report:
        (ROOT / 'analysis/network-console-validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['cases', 'original_functions',
        'returning_assertions', 'required_service_contracts', 'all_results_match']}, indent=2))


if __name__ == '__main__':
    main()
