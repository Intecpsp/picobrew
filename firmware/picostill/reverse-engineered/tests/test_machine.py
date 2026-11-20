"""Compare whole owned machine command/setup effects with original instructions."""
import ctypes as c,gzip,hashlib,json,os,shlex,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import machine_cases
class MachineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='still-machine-');cls.addClassCleanup(cls.temp.cleanup);out=Path(cls.temp.name)/'machine.so'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-ffp-contract=off','-fPIC','-shared','-I',str(ROOT/'include'),str(ROOT/'tests/machine_bridge.cpp')]+[str(p) for p in sorted((ROOT/'src').glob('*.cpp'))]+['-o',str(out)],check=True,capture_output=True,text=True)
        cls.native=c.CDLL(str(out));cls.native.machine_run.argtypes=[c.c_uint,c.POINTER(c.c_uint8),c.c_char_p,c.POINTER(c.c_uint8),c.c_uint,c.POINTER(c.c_uint8),c.c_uint64,c.c_uint,c.c_uint];cls.native.machine_run.restype=None;cls.native.machine_assertion_count.restype=c.c_uint;cls.native.machine_persisted.restype=c.POINTER(c.c_uint8)
        for name,args,result in (('event_count',[],c.c_uint),('event',[c.c_uint],c.c_char_p),('clock',[],c.c_uint32)):
            fn=getattr(cls.native,'controller_'+name);fn.argtypes=args;fn.restype=result
        cls.raw=(ROOT/'tests/fixtures/machine-inputs.json.gz').read_bytes();cls.inputs=json.loads(gzip.decompress(cls.raw));cls.output=(ROOT/'tests/fixtures/machine-original.json.gz').read_bytes();cls.fixture=json.loads(gzip.decompress(cls.output))
    def test_original_inputs_and_provenance(self):
        self.assertEqual(self.inputs,machine_cases.cases());self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['cases_input_gzip_sha256']);self.assertEqual(hashlib.sha256((ROOT.parent/'oem/upstream/picostill_0_0_35.bin').read_bytes()).hexdigest(),self.fixture['input_sha256']);self.assertEqual(self.raw[4:8],bytes(4));self.assertEqual(self.output[4:8],bytes(4))
        for address in ('0x400d7510','0x400d8a00','0x400d764c','0x400d5918','0x400d5938','0x400d2c90'):self.assertGreater(self.fixture['original_functions_executed'][address],0)
    def run_row(self,row,op=None):
        data=(c.c_uint8*0x2000).from_buffer_copy(bytes.fromhex(row['data']));serial=(c.c_uint8*len(row['serial']))(*row['serial']);stored=(c.c_uint8*100).from_buffer_copy(bytes.fromhex(row['stored']));self.native.machine_run(['dispatch','queue','receive','setup'].index(row['op']) if op is None else op,data,row['command'].encode('latin1'),serial,len(serial),stored,row['mac'],row['configured'],row['now']);return bytes(data)
    def test_machine_effects_match_original(self):
        self.assertEqual(len(self.inputs),len(self.fixture['cases']))
        for row,expected in zip(self.inputs,self.fixture['cases']):
            with self.subTest(id=row['id']):
                actual=self.run_row(row);golden=bytes.fromhex(expected['data']);self.assertEqual([(i,a,b) for i,(a,b) in enumerate(zip(actual,golden)) if a!=b],[]);self.assertEqual([self.native.controller_event(i).decode() for i in range(self.native.controller_event_count())],expected['events']);self.assertEqual(self.native.machine_assertion_count(),expected['assertion_count']);self.assertEqual(self.native.controller_clock(),expected['clock'])
    def test_complete_target_link_evidence_matches_checked_source(self):
        raw=(ROOT/'analysis/esp32-build.json').read_bytes();build=json.loads(raw);link=json.loads((ROOT/'analysis/esp32-application-link.json').read_text())
        self.assertTrue(link['complete_application_linked']);self.assertFalse(link['flash_image_produced']);self.assertFalse(link['hardware_tested']);self.assertFalse(link['installation_qualified']);self.assertEqual(link['reconstructed_object_report_sha256'],hashlib.sha256(raw).hexdigest())
        for entry in build['sources']:self.assertEqual(entry['source_sha256'],hashlib.sha256((ROOT/entry['source']).read_bytes()).hexdigest(),entry['source'])
        for field in ('headers','platform_headers'):
            for name,digest in build[field].items():self.assertEqual(digest,hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),name)
        for name in ('app_main','setup()','loop()','picostill::oem::Machine::setup()','picostill::oem::Machine::dispatch_console()','picostill::oem::HttpServer::poll()','picostill::oem::Backend::poll()','picostill::oem::FirmwareUpdate::poll()'):self.assertIn(name,link['required_application_symbols'])
        self.assertEqual(build['build_script_sha256'],hashlib.sha256((ROOT/'tools/build_esp32_objects.py').read_bytes()).hexdigest());self.assertEqual(link['build_script_sha256'],hashlib.sha256((ROOT/'tools/link_esp32_application.py').read_bytes()).hexdigest())
    def test_embedded_recovered_self_tests_preserve_credentials(self):
        row=self.inputs[-1];self.run_row(row,4);self.assertEqual(self.native.machine_assertion_count(),0);self.assertEqual(bytes(self.native.machine_persisted()[:100]),bytes.fromhex(row['stored']));self.assertIn('message:All unit tests PASSED',[self.native.controller_event(i).decode() for i in range(self.native.controller_event_count())])
if __name__=='__main__':unittest.main()
