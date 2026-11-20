#!/usr/bin/env python3
"""Regenerate complete backend HTTP traces from a byte-verified Z119 project."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import run_msp_emulation as common
from http_backend_network_cases import cases

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['project', 'ghidra-home', 'java-home', 'output']:
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Select a new output directory; preserve earlier evidence')
    raw = common.INPUT.read_bytes()
    sha = hashlib.sha256(raw).hexdigest()
    if sha != common.images.IMAGES[common.INPUT.name]:
        raise ValueError('Preserved image identity differs')
    parsed = common.images.parse_image(raw)
    evidence = common.shared.tool_evidence(args.ghidra_home.resolve(), args.java_home.resolve(), {
        'ghidra': args.ghidra_home.parent / common.shared.TOOL_PINS['ghidra']['filename'],
        'jdk': args.java_home.parent / common.shared.TOOL_PINS['jdk']['filename']}, 'Xtensa:LE:32:default')
    args.output.mkdir(parents=True)
    scripts = [ROOT / 'tests/ghidra' / name for name in
               ['BackendConsoleExecution.java', 'EmulateHttpBackendNetwork.java']]
    with tempfile.TemporaryDirectory(prefix='http-backend-network-original-') as directory:
        directory = Path(directory)
        for path in scripts:
            shutil.copyfile(path, directory / path.name)
        layout = directory / 'layout.json'
        layout.write_text(json.dumps(dict(input_path=str(common.INPUT), segments=[dict(
            address=s['load_address'], offset=s['file_offset'], size=s['size_bytes']) for s in parsed['segments']])))
        inputs = args.output / 'http-backend-network-inputs.json'
        inputs.write_text(json.dumps(cases(), separators=(',', ':'))+'\n')
        output = args.output / 'http-backend-network-original.json'
        command = [str(args.ghidra_home / 'support/analyzeHeadless'), str(args.project), 'firmware',
                   '-process', common.INPUT.name, '-readOnly', '-noanalysis', '-max-cpu', '1',
                   '-scriptPath', str(directory), '-postScript', scripts[1].name,
                   str(inputs), str(output), str(layout)]
        env = dict(os.environ, JAVA_HOME=str(args.java_home))
        env['PATH'] = str(args.java_home / 'bin')+os.pathsep+env.get('PATH', '')
        with (args.output / 'execution.log').open('w') as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=1200)
        result = json.loads(output.read_text())
        if result['input_sha256'] != sha or len(result['cases']) != len(cases()):
            raise ValueError('Incomplete original output')
    (args.output / 'execution-tools.json').write_text(json.dumps(dict(tools=evidence,
        script_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in scripts},
        cases=len(cases()), project_read_only=True, analysis=False,
        instruction_budget_per_function=1500000), indent=2)+'\n')
    print('Wrote', len(cases()), 'complete original backend/marker cases')


if __name__ == '__main__':
    main()
