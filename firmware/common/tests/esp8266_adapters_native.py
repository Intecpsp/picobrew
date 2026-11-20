"""Build the actual owned adapters with explicit SDK/physical I/O boundaries."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SDK_SOURCES=['spiffs_hydrogen','spiffs_nucleus','spiffs_cache','spiffs_gc','spiffs_check']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def verify_sdk(sdk):
    pin=json.loads((ROOT/'dependencies/esp8266-adapters.json').read_text())
    for name,expected in pin['sdk_identity_sha256'].items():
        if digest(sdk/name)!=expected:raise ValueError('Pinned SDK mismatch: '+name)

def build_and_run(output,sdk,kind,i386=None):
    output=Path(output);output.mkdir(parents=True,exist_ok=True);sdk=Path(sdk)
    flags=['-O2','-Wall','-Wextra','-Werror'];emulator=[]
    if kind=='capacity_esp32':
        flags+=['-DPICOBREW_ESP32_SPIFFS_SIZING','-DSPIFFS_GC_MAX_RUNS=10',
                '-DSPIFFS_OBJ_META_LEN=4','-DSPIFFS_USE_MAGIC_LENGTH=1',
                '-DSPIFFS_ALIGNED_OBJECT_INDEX_TABLES=0']
    if i386:
        i386=Path(i386);sysroot=i386/'sysroot'
        flags+=['-m32','-static','-msse2','-mfpmath=sse',
                '-isystem',str(sysroot/'usr/include/x86_64-linux-gnu/c++/14/32'),
                '-isystem',str(sysroot/'usr/include'),'-isystem',str(sysroot/'usr/include/x86_64-linux-gnu'),
                '-isystem','/usr/include/x86_64-linux-gnu',
                '-B'+str(sysroot/'usr/lib/gcc/x86_64-linux-gnu/14/32')+'/',
                '-B'+str(sysroot/'usr/lib32')+'/', '-L'+str(sysroot/'usr/lib32')]
        emulator=[str(i386/'usr/bin/qemu-i386')]
    includes=[ROOT/'tests/esp8266_sdk_host',ROOT/'include',ROOT/'platform/esp8266',
              sdk/'cores/esp8266',sdk/'tools/sdk/include',sdk/'tools/sdk/lwip/include']
    include_flags=['-I'+str(p) for p in includes]
    if kind in ('storage','capacity','capacity_esp32'):
        sources=[ROOT/'src/verified_storage.cpp',ROOT/'platform/esp8266/storage.cpp',
                 ROOT/('tests/esp8266_'+('capacity' if kind.startswith('capacity') else kind)+'_test.cpp')]
        if kind.startswith('capacity'):
            sources += [ROOT/('src/'+name+'.cpp') for name in
                        ('engine','api','values','records','history','history_records','telemetry')]
        objects=[]
        for name in SDK_SOURCES:
            obj=output/(name+'.o');source=sdk/('cores/esp8266/spiffs/'+name+'.c')
            subprocess.run(['gcc','-std=c99']+flags+['-Wno-unused-parameter','-Wno-sign-compare','-Wno-stringop-truncation']+include_flags+
                           ['-c',str(source),'-o',str(obj)],check=True)
            objects.append(obj)
        if kind.startswith('capacity'):
            obj=output/'http_server.o'
            subprocess.run(['gcc','-std=c99']+flags+include_flags+
                           ['-c',str(ROOT/'src/http_server.c'),'-o',str(obj)],check=True)
            objects.append(obj)
    elif kind=='telemetry':
        sources=[ROOT/'platform/esp8266/telemetry.cpp',ROOT/'tests/esp8266_telemetry_test.cpp'];objects=[]
    else:raise ValueError(kind)
    executable=output/(kind+'-test')
    subprocess.run(['g++','-std=c++11','-DLWIP_OPEN_SRC']+flags+include_flags+
                   [str(p) for p in sources+objects]+['-o',str(executable)],check=True)
    return json.loads(subprocess.check_output(emulator+[str(executable)],text=True))
