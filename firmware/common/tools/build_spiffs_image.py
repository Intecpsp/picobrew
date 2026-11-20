#!/usr/bin/env python3
"""Create a deterministic, verified SPIFFS image for a modified reference profile."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import spiffs_format as spiffs


def build(profile, esp8266, esp32, output, files=(), work=None):
    geometry = spiffs.PROFILES[profile]
    if profile.startswith('esp32') and esp32 is None:
        raise ValueError('ESP32 profile requires the pinned ESP32 SDK path')
    identities = spiffs.verify_dependencies(esp8266, esp32 if profile.startswith('esp32') else None)
    staged = spiffs.staged_files(files, geometry['partition_bytes'])
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='picobrew-spiffs-image-', dir=work) as temporary:
        directory = Path(temporary)
        binary = spiffs.compile_formatter(directory, esp8266, profile)
        # Copy every explicit input once so native readback and the independent
        # decoder use one snapshot even if the source changes during the run.
        args = [str(binary), str(geometry['partition_bytes']), str(geometry['block_bytes']),
                str(geometry['partition_offset']), str(directory / 'image.bin')]
        for index, (name, (_, data)) in enumerate(staged.items()):
            path = directory / ('input-' + str(index))
            path.write_bytes(data)
            args += [name, str(path)]
        native = json.loads(subprocess.check_output(args, text=True))
        data = (directory / 'image.bin').read_bytes()
        decoded = spiffs.inspect_image(data, profile, {name: item[1] for name, item in staged.items()})
        with tempfile.NamedTemporaryFile(prefix=output.name + '.', suffix='.tmp',
                                         dir=output.parent, delete=False) as stream:
            pending = Path(stream.name)
            try:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
                os.chmod(pending, 0o600 if staged else 0o644)
                os.replace(pending, output)
            finally:
                if pending.exists():
                    pending.unlink()
    return dict(schema_version=1, profile=profile, geometry=geometry,
                image_bytes=len(data), image_sha256=hashlib.sha256(data).hexdigest(),
                staged_private_inputs=bool(staged),
                files=[dict(name=name, bytes=len(item[1])) for name, item in staged.items()],
                native=native, independent_decode=decoded, sdk_identity_sha256=identities,
                formatter_sha256={name: spiffs.digest(spiffs.ROOT / name) for name in
                    ('tools/spiffs_format.py', 'tools/spiffs_image_native.cpp',
                     'tools/build_spiffs_image.py', 'dependencies/spiffs-format-abi.json')},
                hardware_tested=False,
                implementation='Pinned unmodified ESP8266 SPIFFS C sources; ESP32 on-flash configuration and packed ABI verified against its actual pinned SDK object. Native mount/readback plus independent image decoding; no ESP32 SDK binary execution is claimed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=spiffs.PROFILES, required=True)
    parser.add_argument('--esp8266-sdk', type=Path, required=True)
    parser.add_argument('--esp32-sdk', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--file', action='append', default=[], metavar='/pb/KEY=LOCAL_FILE')
    args = parser.parse_args()
    try:
        report = build(args.profile, args.esp8266_sdk, args.esp32_sdk, args.output, args.file)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'SPIFFS image creation failed: ' + str(error) + '\n')
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n')
        if report['staged_private_inputs']:
            os.chmod(args.report, 0o600)
    print(json.dumps(dict(profile=report['profile'], bytes=report['image_bytes'],
                         sha256=report['image_sha256'], files=len(report['files']),
                         staged_private_inputs=report['staged_private_inputs'],
                         remount_verified=True, independently_decoded=True)))


if __name__ == '__main__':
    main()
