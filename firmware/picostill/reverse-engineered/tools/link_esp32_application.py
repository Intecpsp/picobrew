#!/usr/bin/env python3
"""Link PicoStill's complete application to the pinned historical ESP32 SDK.

Produces an ELF and map, never a partition table, bootloader, flash package or
installation command. The original SDK main.cpp owns app_main and loopTask.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def verify_identity(directory,pin):
    for name,expected in json.loads(pin.read_text())['identity_sha256'].items():
        if digest(directory/name)!=expected:raise ValueError('Pinned identity mismatch: '+name)
def verify_objects(directory):
    report=json.loads((directory/'build-report.json').read_text())
    for entry in report['sources']:
        source=ROOT/entry['source'];obj=directory/(entry['source'].replace('/','_')+'.o')
        if digest(source)!=entry['source_sha256'] or digest(obj)!=entry['object_sha256']:raise ValueError('Stale source/object: '+entry['source'])
    for field in ('headers','platform_headers'):
        for name,expected in report[field].items():
            if digest(ROOT/name)!=expected:raise ValueError('Stale compiled header: '+name)
    if digest(directory/'libpicostill_recovered_esp32.a')!=report['archive_sha256']:raise ValueError('Reconstructed archive changed')
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--toolchain',type=Path,required=True);parser.add_argument('--arduino',type=Path,required=True);parser.add_argument('--objects',type=Path,default=ROOT/'build/esp32');parser.add_argument('--output',type=Path,default=ROOT/'build/application');args=parser.parse_args()
    toolchain,arduino,objects,output=[p.resolve() for p in (args.toolchain,args.arduino,args.objects,args.output)]
    verify_identity(toolchain,ROOT/'dependencies/toolchain.json');verify_identity(arduino,ROOT/'dependencies/arduino-esp32.json');report=verify_objects(objects);output.mkdir(parents=True,exist_ok=True)
    binary=toolchain/'bin';cc=binary/'xtensa-esp32-elf-gcc';cxx=binary/'xtensa-esp32-elf-g++';ar=binary/'xtensa-esp32-elf-ar';sdk=arduino/'tools/sdk'
    includes=[ROOT/'include',ROOT/'platform/esp32',arduino/'cores/esp32',arduino/'variants/esp32'];sources=[p for p in sorted((arduino/'cores/esp32').iterdir()) if p.suffix in ('.c','.cpp','.S')]
    for library in ('Wire','WiFi','Update'):
        folder=arduino/'libraries'/library/'src';includes.append(folder);sources.extend(p for p in sorted(folder.rglob('*')) if p.suffix in ('.c','.cpp','.S'))
    includes.extend(sorted(p for p in (sdk/'include').iterdir() if p.is_dir()));include_flags=['-I'+str(p) for p in includes]
    common=[f for f in report['flags'] if f not in ('-std=gnu++11','-Werror','-fno-exceptions','-fno-rtti')]+['-w'];sdk_objects=[];evidence=[]
    for source in sources:
        relative=source.relative_to(arduino).as_posix();obj=output/('sdk_'+relative.replace('/','_')+'.o');cache=output/(obj.name+'.json')
        compiler=cxx if source.suffix=='.cpp' else cc;language=['-std=gnu++11','-fno-exceptions','-fno-rtti'] if source.suffix=='.cpp' else ['-std=gnu99'] if source.suffix=='.c' else ['-x','assembler-with-cpp']
        identity=dict(source_sha256=digest(source),compiler_sha256=digest(compiler),flags=language+common+include_flags);previous=json.loads(cache.read_text()) if cache.exists() else {}
        if not obj.exists() or previous.get('identity')!=identity or previous.get('object_sha256')!=digest(obj):
            subprocess.run([str(compiler)]+language+common+include_flags+['-c',str(source),'-o',str(obj)],check=True);cache.write_text(json.dumps(dict(identity=identity,object_sha256=digest(obj)))+'\n')
        sdk_objects.append(obj);evidence.append(dict(source=relative,source_sha256=digest(source),object_sha256=digest(obj)))
    archive=output/'libarduino_required.a'
    if archive.exists():archive.unlink()
    subprocess.run([str(ar),'rcs',str(archive)]+[str(p) for p in sdk_objects],check=True)
    properties=dict(line.split('=',1) for line in (arduino/'platform.txt').read_text().splitlines() if line and not line.startswith('#') and '=' in line);libraries=properties['compiler.c.elf.libs'].split()
    elf=output/'picostill-application.elf';map_file=elf.with_suffix('.map')
    flags=['-nostdlib','-L'+str(sdk/'lib'),'-L'+str(sdk/'ld'),'-T','esp32_out.ld','-T','esp32.common.ld','-T','esp32.rom.ld','-T','esp32.peripherals.ld','-T','esp32.rom.spiram_incompatible_fns.ld','-u','ld_include_panic_highint_hdl','-u','call_user_start_cpu0','-u','app_main','-Wl,--gc-sections','-Wl,-static','-Wl,--undefined=uxTopUsedPriority','-u','__cxa_guard_dummy','-u','__cxx_fatal_exception','-Wl,-Map='+str(map_file),'-Wl,--cref','-Wl,--start-group',str(objects/'libpicostill_recovered_esp32.a'),str(archive)]+libraries+['-Wl,--end-group','-Wl,-EL','-o',str(elf)]
    subprocess.run([str(cc)]+flags,check=True)
    symbols=subprocess.check_output([str(binary/'xtensa-esp32-elf-nm'),'-C','-n',str(elf)],text=True)
    required=[' app_main',' setup()',' loop()','picostill::oem::Machine::setup()','picostill::oem::Machine::dispatch_console()','picostill::oem::Application::poll()','picostill::oem::Controller::poll()','picostill::oem::HttpServer::poll()','picostill::oem::Backend::poll()','picostill::oem::FirmwareUpdate::poll()']
    for name in required:
        if name not in symbols:raise ValueError('Missing linked application symbol: '+name)
    retained={}
    for line in symbols.splitlines():
        if any(name in line for name in required):
            address,kind,name=line.split(None,2);retained[name]=hex(int(address,16))
    size=subprocess.check_output([str(binary/'xtensa-esp32-elf-size'),str(elf)],text=True).strip()
    result=dict(schema_version=1,complete_application_linked=True,hardware_tested=False,flash_image_produced=False,installation_qualified=False,elf_sha256=digest(elf),map_sha256=digest(map_file),sdk_archive_sha256=digest(archive),sdk_objects=evidence,required_application_symbols=retained,compiler=report['compiler'],reconstructed_object_report_sha256=digest(objects/'build-report.json'),build_script_sha256=digest(__file__),sdk_main_sha256=digest(arduino/'cores/esp32/main.cpp'),original_linker_scripts={p.name:digest(p) for p in sorted((sdk/'ld').glob('*.ld'))},size=size,scope='Complete recovered PicoStill machine composition with concrete SDK WiFi/Client/Update/NVS/GPIO/I2C/serial/partition adapters and original SDK main. No device partition layout or physical qualification is inferred.')
    (output/'link-report.json').write_text(json.dumps(result,indent=2)+'\n');print(size);print('Linked complete PicoStill application ELF/map; no flash package produced')
if __name__=='__main__':main()
