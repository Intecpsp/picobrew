#!/usr/bin/env python3
"""Accept buzzer source only after independent original-instruction comparison."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import buzzer_cases as inputs
import buzzer_native as native
import run_msp_emulation as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-directory', type=Path, required=True)
    parser.add_argument('--write-evidence', action='store_true')
    args = parser.parse_args()
    fixture = args.original_directory / 'buzzer-original.json'
    report = json.loads((args.original_directory / 'buzzer-validation.json').read_text())
    original = json.loads(fixture.read_text())
    cases = inputs.cases()
    if report['fixture_sha256'] != common.shared.digest(fixture):
        raise ValueError('Original fixture differs from its execution report')
    image_sha = common.shared.digest(common.INPUT)
    if report['input_sha256'] != image_sha or original['input_sha256'] != image_sha:
        raise ValueError('Original image differs')
    if len(original['cases']) != len(cases) or report['cases'] != len(cases):
        raise ValueError('Original execution count differs')
    for path, expected in report['generator_files'].items():
        if common.shared.digest(ROOT / path) != expected:
            raise ValueError('Original generator differs: ' + path)
    raw = common.INPUT.read_bytes()
    parsed = common.images.parse_image(raw)
    segments = [dict(address=s['load_address'], offset=s['file_offset'], size=s['size_bytes']) for s in parsed['segments']]
    for function in original['original_functions']:
        for span in function['ranges']:
            if hashlib.sha256(common.read_mapped(raw, segments, span['address'], span['size'])).hexdigest() != span['sha256']:
                raise ValueError('Executed original function bytes differ')
    with tempfile.TemporaryDirectory(prefix='buzzer-validation-') as directory:
        library = native.load_native(directory)
        for case, expected in zip(cases, original['cases']):
            actual = native.run(library, case)
            if actual != expected:
                raise ValueError(json.dumps(dict(case=case, native=actual, original=expected)))
    report['native_validation'] = dict(all_results_match=True, cases=len(cases),
        scope='Buzzer PWM setup/enable/timer calls, System alarm flag/date/returning-assertion continuation, five console registrations and commands, and the Core buzzer initializer slice',
        complete_application_linked=False)
    paths = ['include/zseries/buzzer.hpp', 'src/buzzer.cpp', 'platform/esp32/buzzer_platform.hpp',
             'platform/esp32/buzzer_platform.cpp', 'tests/buzzer_bridge.cpp', 'tests/buzzer_native.py',
             'tests/test_buzzer.py', 'tools/validate_buzzer.py']
    report['validated_source_sha256'] = {path: common.shared.digest(ROOT / path) for path in paths}
    report['original_functions'] = original['original_functions']
    report['modeled_boundaries'] = original['modeled_boundaries']
    report['remaining_scope'] = [
        'LEDC, time, formatting, C++ console storage and int32-representable atoi are explicitly modeled SDK/library boundaries.',
        'The constructor proof executes only the Buzzer member-initializer slice with documented register preconditions.',
        'The System alarm assertion helper returns after logging/counter/delay service effects; the recovered body continues exactly after that callback.',
        'Caller must serialize Core, console and FreeRTOS timer accesses and install real console/assertion/diagnostic services.',
        'No physical audio, full application link, boot, flash or hardware qualification is claimed.']
    if args.write_evidence:
        destination = ROOT / 'tests/fixtures/buzzer-original.json.gz'
        destination.write_bytes(gzip.compress(fixture.read_bytes(), mtime=0))
        report['compressed_fixture_sha256'] = common.shared.digest(destination)
        (ROOT / 'analysis/buzzer-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'All {len(cases)} original buzzer/alarm scenarios match')


if __name__ == '__main__':
    main()
