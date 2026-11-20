#!/usr/bin/env python3
"""Run the native comparison, then record identities of the checked HTTP source."""
import gzip,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    subprocess.run([sys.executable,'-m','unittest','discover','-s',str(ROOT/'tests'),'-p','test_http.py'],check=True,cwd=ROOT)
    original=json.loads(gzip.decompress((ROOT/'tests/fixtures/http-original.json.gz').read_bytes()))
    files=['include/picoferm/core.hpp','include/picoferm/hardware.hpp','include/picoferm/data.hpp','include/picoferm/transport.hpp','src/core.cpp','src/hardware.cpp','src/data.cpp',
        'include/picoferm/http.hpp','include/picoferm/http_server.hpp','src/http.cpp','src/http_server.cpp','src/http_assets.inc','platform/esp8266/http_ports.hpp','platform/esp8266/http_ports.cpp','assets/http-page-prefix.html',
        'tests/http_bridge.cpp','tests/formatter.hpp','tests/test_http.py','tests/fixtures/http-inputs.json.gz','tests/fixtures/http-original.json.gz','tools/http_cases.py','tools/extract_http_assets.py','tools/run_http_emulation.py','tools/build_http_objects.py','tools/record_http_evidence.py',
        'tools/ghidra/PicoFermExecution.java','tools/ghidra/EmulatePicoFermCore.java','tools/ghidra/EmulatePicoFermHardware.java','tools/ghidra/EmulatePicoFermData.java','tools/ghidra/EmulatePicoFermHttp.java']
    report=dict(schema_version=1,input_sha256=original['input_sha256'],cases=len(original['cases']),all_original_comparisons_pass=True,native_tests=3,
        source_sha256={p:digest(ROOT/p) for p in files},original_functions_executed=original['original_functions_executed'],modeled_boundaries=original['modeled_boundaries'],
        scope=original['scope'],owned_rejections=['URI length >=256 bytes','body length >=512 bytes','null URL-decoder inputs or zero destination capacity'],
        hardware_tested=False,full_application_parity_established=False)
    (ROOT/'analysis/http-validation.json').write_text(json.dumps(report,indent=2)+'\n');print('Recorded',report['cases'],'matching complete HTTP cases')
if __name__=='__main__':main()
