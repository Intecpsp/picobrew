#!/usr/bin/env python3
"""Compare original Azure live/password diagnostics without publishing secrets."""
from pathlib import Path
import argparse
import base64
import gzip
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from azure_console_cases import cases
from azure_console_native import build, execute
import recover_backend_credentials as resources

FIXTURE = ROOT / 'tests/fixtures/azure-console-original.json.gz'
REPORT = ROOT / 'analysis/azure-console-validation.json'
SOURCES = [
    'include/zseries/azure.hpp', 'src/azure.cpp',
    'include/zseries/azure_console_tests.hpp', 'src/azure_console_tests.cpp',
    'src/azure_console_password_vectors.inc',
    'platform/esp32/azure_mqtt_platform.hpp', 'platform/esp32/azure_mqtt_platform.cpp',
    'include/zseries/application_console.hpp', 'include/zseries/diagnostics.hpp',
    'src/diagnostics.cpp', 'include/zseries/enums.hpp', 'src/enums.cpp',
    'include/zseries/http_backend.hpp', 'include/zseries/wifi.hpp',
    'tests/azure_console_bridge.cpp', 'tests/azure_console_native.py',
    'tests/azure_console_cases.py', 'tests/run_azure_console_emulation.py',
    'tests/test_azure_console.py', 'tools/validate_azure_console.py',
    'tools/recover_backend_credentials.py', 'tools/ghidra/EmulateAzureConsole.java',
    'tools/ghidra/Z119Execution.java',
]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def encoded(value):
    return (json.dumps(value, separators=(',', ':')) + '\n').encode()


def serialized_inputs():
    return encoded(cases())


def load_fixture():
    return json.loads(gzip.decompress(FIXTURE.read_bytes()))


def fail_contracts(fn):
    sample = cases()[0]
    checks = 0
    for missing in range(1, 11):
        if execute(fn, sample, missing=missing)['result'] != 2:
            raise AssertionError('Missing Azure diagnostic service did not fail: %d' % missing)
        checks += 1
    for kind in range(1, 6):
        chosen = next(c for c in cases() if c['id'] == 'stack-0') if kind == 1 else sample
        for occurrence in (1, 2, 3):
            if execute(fn, chosen, failure_kind=kind, failure_at=occurrence)['result'] != 3:
                raise AssertionError('Rejected Azure callback did not fail: %d/%d' % (kind, occurrence))
            checks += 1
    password = next(c for c in cases() if c['id'] == 'password-vectors')
    for missing in (1, 2, 9, 10):
        if execute(fn, password, missing=missing)['result'] != 2:
            raise AssertionError('Missing password diagnostic service did not fail')
        checks += 1
    for kind, name in ((1, 'password-host-1'), (2, 'password-vectors'), (5, 'password-hmac-fails-1')):
        chosen = next(c for c in cases() if c['id'] == name)
        if execute(fn, chosen, failure_kind=kind, failure_at=1)['result'] != 3:
            raise AssertionError('Rejected password diagnostic callback did not fail')
        checks += 1
    return checks


def verify_private_resources():
    from analyze_images import parse_image
    original = (ROOT.parent / 'oem/upstream/zseries_0_0_119.bin').read_bytes()
    segments = parse_image(original)['segments']
    address = 0x3f42a4dc
    segment = next(s for s in segments if s['load_address'] <= address < s['load_address'] + s['size_bytes'])
    offset = segment['file_offset'] + address - segment['load_address']
    key = original[offset:offset + 32]
    patterns = [key, key.hex().encode(), base64.b64encode(key),
                ','.join(str(b) for b in key).encode(),
                ','.join('0x%02x' % b for b in key).encode()]
    documents = [(ROOT / name).read_bytes() for name in SOURCES]
    if FIXTURE.exists():
        documents.append(gzip.decompress(FIXTURE.read_bytes()))
    if any(pattern in document for pattern in patterns for document in documents):
        raise AssertionError('Private original Azure key leaked into public source or evidence')
    return len(patterns)


def target(toolchain, arduino, output):
    import acquire_esp32
    from build_esp32_objects import ESP32_PACKAGES, SDK_IDENTITIES
    acquire_esp32.verify_identity(toolchain, ESP32_PACKAGES['xtensa-esp32-elf'])
    for path, expected in SDK_IDENTITIES.items():
        if digest(arduino / path) != expected:
            raise ValueError('Pinned SDK identity differs: ' + path)
    compiler = toolchain / 'bin/xtensa-esp32-elf-g++'
    includes = [ROOT / 'include', ROOT / 'platform/esp32', arduino / 'cores/esp32', arduino / 'variants/esp32']
    includes += [arduino / 'libraries' / name / 'src' for name in ('Wire', 'SPI', 'WiFi', 'WiFiClientSecure', 'Update')]
    includes += sorted(p for p in (arduino / 'tools/sdk/include').iterdir() if p.is_dir())
    flags = ['-std=gnu++11', '-Os', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
             '-ffunction-sections', '-fdata-sections', '-fstrict-volatile-bitfields', '-mlongcalls',
             '-fno-fast-math', '-ffp-contract=off', '-fno-exceptions', '-fno-rtti', '-DESP_PLATFORM',
             '-DESP32', '-DARDUINO_ARCH_ESP32', '-DARDUINO=10805', '-DF_CPU=240000000L',
             '-DCORE_DEBUG_LEVEL=0', '-DHAVE_CONFIG_H']
    results, headers = [], set()
    for name in ('src/azure.cpp', 'src/azure_console_tests.cpp', 'platform/esp32/azure_mqtt_platform.cpp'):
        obj = output / (name.replace('/', '_') + '.o')
        dependencies = obj.with_suffix('.d')
        subprocess.run([str(compiler), *flags, *['-I' + str(p) for p in includes], '-MMD', '-MF',
                        str(dependencies), '-c', str(ROOT / name), '-o', str(obj)], check=True)
        results.append(dict(source=name, source_sha256=digest(ROOT / name),
                            object_bytes=obj.stat().st_size, object_sha256=digest(obj)))
        for token in dependencies.read_text().replace('\\\n', ' ').split()[1:]:
            path = Path(token)
            if path.is_file() and path.is_relative_to(ROOT) and path.suffix in ('.hpp', '.h', '.inc'):
                headers.add(path.relative_to(ROOT).as_posix())
    return dict(compiler=subprocess.check_output([str(compiler), '--version'], text=True).splitlines()[0],
                compiler_sha256=digest(compiler), flags=flags, sources=results,
                owned_header_sha256={name: digest(ROOT / name) for name in sorted(headers)},
                sdk_identity_sha256=SDK_IDENTITIES)


def sanitize(output):
    rows = ['{' + ','.join(map(str, c['words'])) + '}' for c in cases()]
    header = output / 'azure-sanitizer-cases.hpp'
    header.write_text('static const std::uint32_t Inputs[][20]={\n' + ',\n'.join(rows) + '\n};\n')
    index = {c['id']: i for i, c in enumerate(cases())}
    main = output / 'azure-sanitizer-main.cpp'
    main.write_text('''#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <vector>
#include "azure-sanitizer-cases.hpp"
extern "C" int azure_console_case(const std::uint32_t*,unsigned,unsigned,unsigned,std::uint32_t*,std::size_t);
int main(){std::vector<std::uint32_t> out(1000000);unsigned count=0;
auto run=[&](unsigned index,unsigned missing,unsigned kind,unsigned occurrence,unsigned expected){
    if(azure_console_case(Inputs[index],missing,kind,occurrence,out.data(),out.size())||out[1]!=expected)return false;
    ++count;return true;};
for(unsigned i=0;i<sizeof(Inputs)/sizeof(Inputs[0]);++i)if(!run(i,0,0,0,0))return 1;
for(unsigned missing=1;missing<=10;++missing)if(!run(0,missing,0,0,2))return 2;
for(unsigned kind=1;kind<=5;++kind)for(unsigned at=1;at<=3;++at)if(!run(kind==1?STACK:0,0,kind,at,3))return 3;
for(unsigned missing:{1u,2u,9u,10u})if(!run(PASSWORD,missing,0,0,2))return 4;
if(!run(HOST,0,1,1,3)||!run(PASSWORD,0,2,1,3)||!run(HMAC,0,5,1,3))return 5;
std::printf("%u Azure diagnostic sanitizer cases passed\\n",count);return 0;}
'''.replace('STACK', str(index['stack-0'])).replace('PASSWORD', str(index['password-vectors']))
        .replace('HOST', str(index['password-host-1'])).replace('HMAC', str(index['password-hmac-fails-1'])))
    sources = [ROOT / ('src/' + name + '.cpp') for name in ('azure_console_tests', 'azure', 'enums', 'diagnostics')]
    executable = output / 'azure-console-sanitized'
    subprocess.run(['g++', '-std=c++11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-no-pie', '-I' + str(ROOT / 'include'), '-I' + str(output / 'generated'),
                    '-I' + str(output), *map(str, sources), str(ROOT / 'tests/azure_console_bridge.cpp'),
                    str(main), '-lcrypto', '-o', str(executable)], check=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    completed = subprocess.run([str(executable)], env=env, check=True, capture_output=True, text=True, timeout=120)
    return dict(passed=True, cases=len(cases()) + 32, output=completed.stdout.strip(), leak_sanitizer=False,
                leak_sanitizer_boundary='Unavailable in ptrace-managed execution environment')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execution', type=Path)
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--arduino', type=Path)
    parser.add_argument('--write-fixture', action='store_true')
    parser.add_argument('--write-report', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/azure-console-native')
    args = parser.parse_args()
    if bool(args.toolchain) != bool(args.arduino):
        parser.error('--toolchain and --arduino must be supplied together')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.execution:
        original = [json.loads(line) for line in (args.execution / 'original.jsonl').read_text().splitlines()]
        metadata = json.loads((args.execution / 'original.jsonl.json').read_text())
        tools = json.loads((args.execution / 'execution-tools.json').read_text())
        fixture = dict(original=original, execution=metadata, tools=tools)
    else:
        fixture = load_fixture()
        original, metadata = fixture['original'], fixture['execution']
    if metadata['input_file_sha256'] != hashlib.sha256(serialized_inputs()).hexdigest():
        raise ValueError('Original execution inputs changed')
    if metadata['input_sha256'] != resources.IMAGE_SHA or metadata['cases'] != len(cases()):
        raise ValueError('Original execution identity/count mismatch')
    fn = build(output)
    actual = [execute(fn, item) for item in cases()]
    if len(original) != len(actual):
        raise ValueError('Incomplete original trace')
    for expected, observed in zip(original, actual):
        if expected != observed:
            raise AssertionError('Original/native Azure comparison failed for ' + observed['id'] + '; traces contain hashes only')
    contracts = fail_contracts(fn)
    if args.write_fixture:
        data = gzip.compress(encoded(fixture), mtime=0)
        if len(data) > 80000:
            raise ValueError('Split original fixture before publishing: exceeds 80 KiB bound')
        FIXTURE.write_bytes(data)
    privacy = verify_private_resources()
    target_result = target(args.toolchain.resolve(), args.arduino.resolve(), output) if args.toolchain else None
    sanitized = sanitize(output) if args.sanitize else None
    counts = {str(kind): sum(event[0] == kind for row in actual for event in row['events'])
              for kind in (16, 17, 18, 23, 24, 40, 51, 52, 53, 54, 55, 56, 57, 58, 59)}
    report = dict(schema_version=1, original_image_sha256=resources.IMAGE_SHA,
        original_entries=['0x400fec74', '0x400fa684'], cases=len(actual),
        ordered_events=sum(len(row['events']) for row in actual), event_counts=counts,
        contract_checks=contracts, private_resource_leak_checks=privacy,
        fixture_sha256=digest(FIXTURE), fixture_uncompressed_sha256=hashlib.sha256(gzip.decompress(FIXTURE.read_bytes())).hexdigest(),
        inputs_sha256=hashlib.sha256(serialized_inputs()).hexdigest(),
        source_sha256={name: digest(ROOT / name) for name in SOURCES}, original_execution=metadata,
        target_compile=target_result, sanitizers=sanitized,
        native_crypto=subprocess.check_output(['openssl', 'version'], text=True).strip(), passed=True,
        scope='Complete original Azure integration/password tests, original signature/lifecycle/backoff functions, five real TestScopes, returning assertions, timeout and watchdog continuation. Ordered diagnostics, credentials and key/input bytes compare as SHA-256. Java JCE and native OpenSSL independently provide the explicit HMAC-SHA256 boundary; MQTT/socket, libc, scheduler and stack remain explicit boundaries. No live OEM requests or hardware qualification.')
    if args.write_report:
        REPORT.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(cases=len(actual), ordered_events=report['ordered_events'], contract_checks=contracts, passed=True)))


if __name__ == '__main__':
    main()
