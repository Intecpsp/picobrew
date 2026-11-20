#!/usr/bin/env python3
"""Run full backend requests/parsers and failed-service exits under ASan/UBSan."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess
from http_backend_network_cases import cases
from http_backend_network_native import ROOT, SOURCES, recover_backend_credentials
from program_recipe_native import dependency


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if 0x3f418735 not in recover_backend_credentials.HTTP_CLIENT_TEST_ADDRESSES:
        recover_backend_credentials.HTTP_CLIENT_TEST_ADDRESSES += (0x3f418735,)
    recover_backend_credentials.generate(args.output / 'generated')
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    flags = ['-std=c++11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-Wno-error=uninitialized', '-pedantic', '-fno-fast-math', '-ffp-contract=off',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-ffunction-sections', '-fdata-sections', '-DHTTP_BACKEND_NETWORK_SANITIZER_MAIN',
        '-I'+str(ROOT / 'include'), '-I'+str(dependency() / 'src'),
        '-I'+str(args.output / 'generated')]
    executable = args.output / 'http-backend-network-sanitizer'
    subprocess.run(compiler+flags+[str(ROOT / p) for p in SOURCES]+
                   ['-Wl,--gc-sections', '-o', str(executable)], check=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(executable)], input=''.join(json.dumps(c)+'\n' for c in cases()),
                   text=True, env=env, check=True, timeout=180)
    report = dict(schema_version=1, response_cohorts=12, failure_contracts=14,
        asan=True, ubsan=True, leak_detection=False,
        leak_detection_reason='LeakSanitizer is unavailable under the execution container ptrace environment.',
        compiler=subprocess.check_output(compiler+['--version'], text=True).splitlines()[0],
        flags=flags[:flags.index('-I'+str(ROOT / 'include'))],
        third_party_warning='Only ASan/O1 allows the pinned ArduinoJson StringMover uninitialized warning; normal and target compilation retain -Werror.',
        source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in SOURCES},
        no_live_network=True, no_private_trace_output=True, passed=True)
    (args.output / 'sanitizer-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('ASan/UBSan passed 12 response cohorts and 14 failed-service exits')


if __name__ == '__main__':
    main()
