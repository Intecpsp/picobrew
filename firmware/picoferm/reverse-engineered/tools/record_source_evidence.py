#!/usr/bin/env python3
"""Record the original bytes reviewed and the source/replay identity."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[2]/'tools/firmware_analysis'))
from esp_image import mapped_offset,parse_esp8266_arduino
ENTRIES={0x40202d74:'diagnostic_assertion',0x40202d8c:'diagnostic_warning',0x40202e30:'read_button',
    0x40203700:'PersistentData::migrate',0x4020381c:'PersistentData::reset',0x40203914:'PersistentData::calibration',
    0x402039fc:'PersistentData::sanitize',0x40203d3c:'PersistentData::append',0x4020da98:'LogIterator::done',
    0x40203b60:'LogIterator::advance',0x40204140:'parse_session, non-null returning domain',
    0x402050b4:'Measurements::persist',0x40205114:'Measurements::on_adc',
    0x402029ac:'Adc::write',0x402029e8:'Adc::read',0x40202a28:'Adc::set',0x40202a68:'Adc::temperature_channel',
    0x40202a7c:'Adc::clear',0x40202abc:'Adc::pressure_channel',0x40202ad0:'Adc::configure_rate',0x40202b00:'Adc::configure_gain',
    0x40202b34:'Adc::configure_reference',0x40202b70:'Adc::wait_set',0x40202ba8:'Adc::wait_clear',0x40202bec:'Adc::calibrate',
    0x40202c14:'Adc::initialize',0x40202c90:'Adc::begin',0x40202cd8:'Adc::read24',0x40202d30:'Adc::sample',
    0x40202db0:'battery_voltage',0x4020319c:'Acquisition::start',0x40203210:'Acquisition::advance',
    0x4020565c:'Leds::set_active',0x402056a4:'Leds::set',0x402058ac:'Leds::set_text',
    0x40202e94:'wifi_status_name',0x40204ab4:'wifi_state_name',0x40204ad0:'Wifi::request',
    0x40205558:'Wifi::initialize',0x402055e4:'Wifi::connect',0x40205b5c:'Wifi::set_state',0x40205d78:'Wifi::fail',0x40205e08:'Wifi::poll',
    0x40207ba8:'Application::poll including ADC, button, LEDs, Wi-Fi, backend and main-state scheduling',
    0x4020348c:'bounded_copy',0x402043c4:'encode_hex',0x40204440:'DeviceData::set_mac',0x40204480:'DeviceData::update_token',
    0x402044e8:'configure_credentials',0x40204578:'DeviceData::initialize',0x40203bbc:'report_storage',
    0x40204350:'Backend::receive_session',0x4020534c:'Buffer<512>::append',0x402053a8:'Backend::request_prefix',
    0x40205420:'Buffer<512>::append string',0x40205450:'Backend::argument',0x40205490:'Buffer<64>::append string',
    0x402054f4:'Backend::set_headers',0x40205630:'Backend::fail',0x40206a28:'Backend::send',0x40206b0c:'Backend::sync',
    0x40207528:'Backend::request_state',0x40207584:'Backend::request_logs',0x402076c4:'Backend::complete',0x40207878:'Backend::poll'}
ENTRIES.update({
    0x40202fe4:'Application::calibrate',0x40203014:'Application::animation_changed',0x40203038:'Application::long_press',
    0x40203088:'Application::restart',0x402030e4:'Application::sleep',0x4020385c:'Application::factory_reset',0x402039cc:'Application::calibration',
    0x40203b30:'Application::load_storage',0x4020484c:'Application::report_raw_storage',0x40204d78:'Application::timed_out',
    0x40204e00:'Application and owned member construction',0x4020526c:'Application::info',0x402057a8:'Application::update',
    0x4020597c:'Application::handle_sync',0x40205a38:'Application::wifi_changed',0x40206bf8:'Application::transition',
    0x40206e80:'Application::setup',0x40206ffc:'Application::begin',0x402070f0:'Application::dispatch_console',0x40207fe8:'Application::loop',
    0x40208004:'Application static initializer',0x4020686c:'Application::run_embedded_tests',
    0x40204680:'data_tests',0x40206758:'buffer_tests',0x402066d4:'Buffer<16>::append',0x40206720:'Buffer<16>::append string',
    0x40204058:'eeprom_tests',0x40203de0:'equal_record',0x402049ac:'memory_tests',0x40204208:'session_tests',
    0x40203410:'parse_request_line',0x402034e4:'decode_form',0x402035d0:'run_cstring_self_tests',0x40204b2c:'parse_http_request',
    0x40204cf8:'run_http_parser_self_tests',0x40205f84:'HttpServer::page_header',0x40202f14:'HttpServer::page_footer',
    0x40206074:'HttpServer::setup_page',0x40206284:'HttpServer::reset_page',0x40206338:'HttpServer::calibration_page',
    0x402063dc:'HttpServer::details_page',0x402064ec:'WifiForm parser',0x40206608:'HttpServer::configure_page',
    0x40206924:'Http route dispatch',0x402069d4:'HttpServer::validate_trigger',0x402079c0:'HttpServer::poll'})

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--export',type=Path,required=True);args=parser.parse_args()
    raw=(ROOT.parent/'oem/upstream/picoferm_0_2_7.bin').read_bytes();image=parse_esp8266_arduino(raw)
    if json.loads((args.export/'manifest.json').read_text())['input']['sha256']!=image['sha256']:raise ValueError('Wrong original image')
    functions={int(f['address'],16):f for f in json.loads((args.export/'functions.json').read_text())};records=[]
    for address,name in sorted(ENTRIES.items()):
        function=functions[address];ranges=[]
        for region in function['ranges']:
            start,end=int(region['start'],16),int(region['end'],16)+1;offset=mapped_offset(image,start,end-start)
            ranges.append(dict(start=start,size_bytes=end-start,sha256=hashlib.sha256(raw[offset:offset+end-start]).hexdigest()))
        records.append(dict(name=name,entry=address,ranges=ranges,original_instruction_count=function['instruction_count'],validation='reviewed constructor/initializer composition and full target link; constructor body not itself replayed' if address in (0x40204e00,0x40208004) else 'original instruction comparison'))
    files=[p.relative_to(ROOT).as_posix() for base in ('include','src','platform') for p in sorted((ROOT/base).rglob('*')) if p.is_file()]
    files+=['tools/build_esp8266_objects.py','analysis/esp8266-build.json','tools/ghidra/PicoFermExecution.java']
    files+=['tests/formatter.hpp','tools/record_source_evidence.py','tools/link_esp8266_application.py','analysis/esp8266-application-link.json','tools/application_layout.py','analysis/application-ghidra-layout.json','analysis/application-ghidra-analysis.json','analysis/http-validation.json','analysis/http-target-build.json']
    for stem in ('core','hardware','wifi','data','backend','application','http'):
        files+=['tools/'+stem+'_cases.py','tools/ghidra/EmulatePicoFerm'+stem.title()+'.java',
                'tests/'+stem+'_bridge.cpp','tests/test_'+stem+'.py','tests/fixtures/'+stem+'-inputs.json.gz','tests/fixtures/'+stem+'-original.json.gz']
    report=dict(schema_version=1,input_sha256=image['sha256'],functions=records,files={name:digest(ROOT/name) for name in files},
        scope='Complete recovered PicoFerm application composition: packed storage, ADC/thermistor/pressure, LEDs/button, Wi-Fi, five-command backend, setup browser and inbound HTTP, complete main state scheduler, console, embedded diagnostics, OTA result handling, startup and sleep. 4066 original instruction cases execute returning assertions and recovered component continuations. SDK/ROM/diagnostic boundaries are explicit; the main-loop HTTP poll boundary is covered separately by 440 full HTTP cases. TwoWire SDK object bytes are normalized: its original assignment copies an uninitialized timeout-start word, outside owned application state.',
        platform_scope='Seventeen owned modules compile and the complete Application/HTTP/Backend/Wi-Fi composition links against pinned Xtensa LX106 GCC 4.8.2 and Arduino ESP8266 2.4.1, using the original SDK startup. SDK 2-MiB reference linker layout agrees with the image header; individual board layout, flash contents, RF, ADC, installation and recovery remain unqualified.',
        deliberate_guards=['null session input returns false after assertion; OEM dereferences null','physical measurement index outside 0..11 rejected','float diagnostic buffer holds extreme finite float formatting; OEM 28-byte temporary is not reproduced as an overflow','ADC advance outside states 0..12 rejects out-of-table access','invalid LED animation or rotating index rejects out-of-table GPIO access','null network credential arguments are represented as empty owned strings after diagnostic','null/zero-capacity string copies and oversized hex output reject invalid memory operations','128-byte firmware URL diagnostics use a bounded 129-byte view without reading past the field'],
        application_reconstruction_implemented=True,reconstruction_complete=False,complete_application_linked=True,hardware_tested=False,
        outstanding=['modified authenticated local API, standalone browser and Hub integration',
                     'board flash/recovery mapping and physical qualification'])
    (ROOT/'analysis/source-evidence.json').write_text(json.dumps(report,indent=2)+'\n');print('Recorded %d reviewed application entries'%len(records))
if __name__=='__main__':main()
