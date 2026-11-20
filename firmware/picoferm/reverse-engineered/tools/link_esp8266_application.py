#!/usr/bin/env python3
"""Link the complete PicoFerm owner to pinned Arduino ESP8266 2.4.1.

Only ELF/map evidence is produced. The SDK 2-MiB reference layout agrees with
this image's header; it does not establish an individual unit's flash layout.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def verify_identity(directory,pin):
    for name,expected in json.loads(pin.read_text())['identity_sha256'].items():
        if digest(directory/name)!=expected:raise ValueError('Dependency identity mismatch: '+name)
def verify_objects(directory):
    report=json.loads((directory/'build-report.json').read_text())
    expected={p.relative_to(ROOT).as_posix()for folder in ('src','platform/esp8266')for p in (ROOT/folder).glob('*.cpp')}
    if {entry['source']for entry in report['sources']}!=expected:raise ValueError('Compiled source inventory is stale')
    for entry in report['sources']:
        source=ROOT/entry['source'];obj=directory/(entry['source'].replace('/','_')+'.o')
        if digest(source)!=entry['source_sha256'] or digest(obj)!=entry['object_sha256']:raise ValueError('Stale source/object: '+entry['source'])
    for name,expected in report['headers'].items():
        if digest(ROOT/name)!=expected:raise ValueError('Stale compiled header: '+name)
    if digest(directory/'libpicoferm_recovered_esp8266.a')!=report['archive_sha256']:raise ValueError('Reconstructed archive changed')
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--toolchain',type=Path,required=True);parser.add_argument('--arduino',type=Path,required=True);parser.add_argument('--objects',type=Path,default=ROOT/'build/esp8266');parser.add_argument('--output',type=Path,default=ROOT/'build/application');args=parser.parse_args()
    toolchain,arduino,objects,output=[p.resolve()for p in (args.toolchain,args.arduino,args.objects,args.output)]
    verify_identity(toolchain,ROOT/'dependencies/toolchain.json');verify_identity(arduino,ROOT/'dependencies/arduino-esp8266.json');report=verify_objects(objects);output.mkdir(parents=True,exist_ok=True)
    binary=toolchain/'bin';cc=binary/'xtensa-lx106-elf-gcc';cxx=binary/'xtensa-lx106-elf-g++';ar=binary/'xtensa-lx106-elf-ar';sdk=arduino/'tools/sdk';core=arduino/'cores/esp8266'
    includes=[ROOT/'include',ROOT/'platform/esp8266',core,arduino/'variants/generic',sdk/'include',sdk/'lwip/include',sdk/'libc/xtensa-lx106-elf/include'];sources=[p for p in sorted(core.rglob('*'))if p.suffix in ('.c','.cpp','.S')]
    for library in ('Wire','EEPROM','DNSServer/src','ESP8266WiFi/src','ESP8266httpUpdate/src','ESP8266HTTPClient/src'):
        folder=arduino/'libraries'/library;includes.append(folder);sources.extend(p for p in sorted(folder.rglob('*'))if p.suffix in ('.c','.cpp','.S')and'examples'not in p.parts)
    includes.extend([core/'umm_malloc',core/'libb64',core/'spiffs']);include_flags=['-I'+str(p)for p in includes]
    common=[f for f in report['flags']if f not in ('-std=c++11','-Werror','-fno-exceptions','-fno-rtti')]+['-w','-DLWIP_OPEN_SRC'];sdk_objects=[];evidence=[]
    for source in sources:
        relative=source.relative_to(arduino).as_posix();obj=output/('sdk_'+relative.replace('/','_')+'.o');cache=output/(obj.name+'.json')
        compiler=cxx if source.suffix=='.cpp'else cc;language=['-std=c++11','-fno-exceptions','-fno-rtti']if source.suffix=='.cpp'else['-std=gnu99']if source.suffix=='.c'else['-x','assembler-with-cpp']
        identity=dict(source_sha256=digest(source),compiler_sha256=digest(compiler),flags=language+common+include_flags);previous=json.loads(cache.read_text())if cache.exists()else{}
        if not obj.exists()or previous.get('identity')!=identity or previous.get('object_sha256')!=digest(obj):
            subprocess.run([str(compiler)]+language+common+include_flags+['-c',str(source),'-o',str(obj)],check=True);cache.write_text(json.dumps(dict(identity=identity,object_sha256=digest(obj)))+'\n')
        sdk_objects.append(obj);evidence.append(dict(source=relative,source_sha256=digest(source),object_sha256=digest(obj)))
    archive=output/'libarduino_required.a'
    if archive.exists():archive.unlink()
    subprocess.run([str(ar),'rcs',str(archive)]+[str(p)for p in sdk_objects],check=True)
    properties=dict(line.split('=',1)for line in (arduino/'platform.txt').read_text().splitlines()if line and not line.startswith('#')and'='in line);libraries=properties['compiler.c.elf.libs'].replace('{build.lwip_lib}','-llwip_gcc').split()
    elf=output/'picoferm-application.elf';map_file=elf.with_suffix('.map');linker='eagle.flash.2m.ld'
    flags=['-nostdlib','-L'+str(sdk/'lib'),'-L'+str(sdk/'ld'),'-L'+str(sdk/'libc/xtensa-lx106-elf/lib'),'-T',linker,'-u','call_user_start','-Wl,--gc-sections','-Wl,-static','-Wl,-wrap,system_restart_local','-Wl,-wrap,spi_flash_read','-Wl,-Map='+str(map_file),'-Wl,--cref','-Wl,--start-group',str(objects/'libpicoferm_recovered_esp8266.a'),str(archive)]+libraries+['-Wl,--end-group','-Wl,-EL','-o',str(elf)]
    subprocess.run([str(cc)]+flags,check=True,cwd=sdk/'lib')
    symbols=subprocess.check_output([str(binary/'xtensa-lx106-elf-nm'),'-C','-n',str(elf)],text=True)
    required=[' call_user_start',' user_init',' setup',' loop','picoferm::oem::Application::setup()','picoferm::oem::Application::poll()','picoferm::oem::Application::dispatch_console()','picoferm::oem::HttpServer::poll()','picoferm::oem::Backend::poll()','picoferm::oem::Wifi::poll()']
    for name in required:
        if name not in symbols:raise ValueError('Missing linked application symbol: '+name)
    retained={}
    for line in symbols.splitlines():
        if any(name in line for name in required):
            address,kind,name=line.split(None,2);retained[name]=hex(int(address,16))
    size=subprocess.check_output([str(binary/'xtensa-lx106-elf-size'),str(elf)],text=True).strip()
    result=dict(schema_version=1,complete_application_linked=True,hardware_tested=False,flash_image_produced=False,installation_qualified=False,elf_sha256=digest(elf),map_sha256=digest(map_file),sdk_archive_sha256=digest(archive),sdk_objects=evidence,required_application_symbols=retained,compiler=report['compiler'],reconstructed_object_report_sha256=digest(objects/'build-report.json'),build_script_sha256=digest(__file__),sdk_main_sha256=digest(core/'core_esp8266_main.cpp'),original_linker_scripts={name:digest(sdk/'ld'/name)for name in [linker,'eagle.app.v6.common.ld','eagle.rom.addr.v6.ld']},size=size,scope='Complete recovered PicoFerm owner and original SDK startup/loop with EEPROM/Wire/Wi-Fi/Client/DNS/update/serial/sleep bindings. SDK 2-MiB layout is a reference matching the image header, not a verified device backup. No flash package, hardware or installation qualification.')
    (output/'link-report.json').write_text(json.dumps(result,indent=2)+'\n');print(size);print('Linked complete PicoFerm application ELF/map; no flash package produced')
if __name__=='__main__':main()
