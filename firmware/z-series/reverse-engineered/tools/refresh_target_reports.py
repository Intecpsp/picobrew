#!/usr/bin/env python3
"""Project current target-only evidence from a verified complete object build.

Original execution, native comparison and sanitizer evidence are never renewed
by this script. The two mixed native/target reports change only target_compile.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import link_esp32_application as link

HEADER_GROUPS = ('owned_header_sha256', 'platform_header_sha256',
                 'adapter_header_sha256', 'implementation_include_sha256')
COPIED_GROUPS = ('common_flags', 'cpp_flags', 'sdk_identity_sha256',
                 'arduinojson_dependency', 'ethernet2_dependency',
                 'ethernet2_objects', 'original_backend_constants')
EMBEDDED_TARGET_SOURCES = {
    'azure-console-validation.json': (
        'src/azure.cpp', 'src/azure_console_tests.cpp',
        'platform/esp32/azure_mqtt_platform.cpp'),
    'http-client-console-validation.json': ('src/http_client_console_tests.cpp',),
}
SCOPE = ('Selected source objects from the complete current pinned ESP32 compilation; '
         'source, header, included-file, dependency, generated-resource and object/archive '
         'identities verified before projection. The linked application has separate '
         'evidence. No hardware execution or installation is claimed.')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def backlink(fresh, source_report_sha, selected):
    return dict(report='integration-target-build.json',
                report_sha256=source_report_sha,
                compiled_modules=len(fresh['sources']),
                selected_sources=len(set(selected)))


def identities_for(fresh):
    identities = {row['source']: row['source_sha256'] for row in fresh['sources']}
    for group in HEADER_GROUPS:
        identities.update(fresh[group])
    identities.update(fresh['build_input_sha256'])
    return identities


def project_flat(name, old, fresh, rows, identities, source_report_sha):
    report = copy.deepcopy(old)
    selected = []
    for key, value in old.items():
        if (isinstance(value, list) and value and all(
                isinstance(item, dict) and 'source' in item and 'source_sha256' in item
                for item in value)):
            replacement = []
            for item in value:
                source = item['source']
                if source not in rows:
                    raise ValueError('Uncompiled target source ' + source)
                row = copy.deepcopy(rows[source])
                selected.append(source)
                if 'object_name' in item:
                    row['object_name'] = source.replace('/', '_') + '.o'
                replacement.append(row)
            report[key] = replacement
        elif isinstance(value, dict) and (key.endswith('sha256') or key == 'source_inputs'):
            updated = {}
            for source, expected in value.items():
                if source in identities:
                    updated[source] = identities[source]
                elif (ROOT / source).is_file():
                    if digest(ROOT / source) != expected:
                        raise ValueError('Unverified changed build input ' + source + ' in ' + name)
                    updated[source] = expected
                else:
                    updated[source] = expected
            report[key] = updated
    if not selected:
        raise ValueError('No target source rows: ' + name)
    for key in old:
        if key in fresh and key not in ('sources', 'scope') and not isinstance(old[key], (dict, list)):
            report[key] = fresh[key]
    for key in COPIED_GROUPS:
        if key in old:
            report[key] = copy.deepcopy(fresh[key])
    if 'flags' in old:
        report['flags'] = fresh['cpp_flags'] + fresh['common_flags']
    if 'compiler_version' in old:
        report['compiler_version'] = fresh['compiler']
    for key in ('compiled_modules', 'modules_compiled'):
        if key in old:
            report[key] = len(rows)
    if 'ethernet2_dependency_objects' in old:
        report['ethernet2_dependency_objects'] = len(fresh['ethernet2_objects'])
    report['complete_build_report_sha256'] = source_report_sha
    report['integration_revalidation'] = backlink(fresh, source_report_sha, selected)
    report['scope'] = SCOPE
    if 'complete_application_linked' in report:
        report['complete_application_linked'] = False
    if 'hardware_tested' in report:
        report['hardware_tested'] = False
    return report


def project_embedded(name, old, fresh, rows, source_report_sha):
    selected = EMBEDDED_TARGET_SOURCES[name]
    if any(source not in rows for source in selected):
        raise ValueError('Missing compiled source for embedded target evidence: ' + name)
    previous = old.get('target_compile')
    if not isinstance(previous, dict):
        raise ValueError('Missing embedded target schema: ' + name)
    target = copy.deepcopy(previous)
    if name == 'azure-console-validation.json':
        if [row['source'] for row in previous.get('sources', [])] != list(selected):
            raise ValueError('Unexpected Azure target source inventory')
        target['sources'] = [copy.deepcopy(rows[source]) for source in selected]
    else:
        # The historical HTTP target record has one scalar source/object pair.
        # Keep that schema and make its previously implicit source explicit.
        row = rows[selected[0]]
        for key in ('source', 'source_sha256', 'object_sha256', 'object_bytes'):
            target[key] = row[key]
    for key in ('compiler', 'compiler_sha256', 'arduino_reference', 'sdk_config_sha256',
                'sdk_identity_sha256', 'esp32_dependency_pin_sha256', 'build_script_sha256'):
        target[key] = copy.deepcopy(fresh[key])
    target['flags'] = fresh['cpp_flags'] + fresh['common_flags']
    # These are the verified full-build input maps, not a claim that every
    # header is included by each selected translation unit. The backlink names
    # that complete build and its exact selected source count.
    for key in HEADER_GROUPS:
        target[key] = copy.deepcopy(fresh[key])
    target['complete_build_report_sha256'] = source_report_sha
    target['integration_revalidation'] = backlink(fresh, source_report_sha, selected)
    target['scope'] = SCOPE + ' Header maps cover the complete build inputs.'
    target['complete_application_linked'] = False
    target['hardware_tested'] = False
    report = copy.deepcopy(old)
    report['target_compile'] = target
    if {key: value for key, value in report.items() if key != 'target_compile'} != {
            key: value for key, value in old.items() if key != 'target_compile'}:
        raise AssertionError('Embedded projection altered non-target evidence')
    return report


def prepare(objects):
    fresh = link.verify_objects(objects)
    source_bytes = (objects / 'build-report.json').read_bytes()
    if json.loads(source_bytes) != fresh:
        raise ValueError('Build report changed during verification')
    rows = {row['source']: row for row in fresh['sources']}
    if len(rows) != len(fresh['sources']):
        raise ValueError('Duplicate source rows in complete build')
    identities = identities_for(fresh)
    source_report_sha = hashlib.sha256(source_bytes).hexdigest()
    integration = ROOT / 'analysis/integration-target-build.json'
    updates = {integration: source_bytes}
    changed = []
    for path in sorted((ROOT / 'analysis').glob('*.json')):
        if path == integration or path.name == 'physical-inputs-target-validation.json':
            continue
        old = json.loads(path.read_text())
        if path.name in EMBEDDED_TARGET_SOURCES:
            report = project_embedded(path.name, old, fresh, rows, source_report_sha)
        elif any(part in path.name for part in ('-target-', '-esp32-build')):
            report = project_flat(path.name, old, fresh, rows, identities, source_report_sha)
        else:
            continue
        if report != old:
            updates[path] = encoded(report)
            changed.append(path.name)
    summary = dict(complete_modules=len(rows), ethernet2_objects=len(fresh['ethernet2_objects']),
                   refreshed_reports=len(changed), embedded_target_reports=[
                       name for name in EMBEDDED_TARGET_SOURCES if name in changed],
                   verified_build_report_sha256=source_report_sha)
    return updates, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    # Validate every projection before writing any report, so a schema/input
    # failure cannot leave half of the evidence at a new revision.
    updates, summary = prepare(args.objects.resolve())
    if not args.dry_run:
        for path, content in updates.items():
            path.write_bytes(content)
    summary['dry_run'] = args.dry_run
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
