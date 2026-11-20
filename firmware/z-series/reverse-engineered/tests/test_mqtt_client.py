import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_mqtt_client as validation


class MqttClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='z-mqtt-test-');cls.addClassCleanup(cls.directory.cleanup)
        cls.binary,_=validation.build(cls.directory.name)
        cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/mqtt-client-original.json.gz').read_bytes()))

    def test_original_packets_callbacks_and_lifecycle(self):
        cases=validation.input_cases();self.assertEqual([r['input'] for r in self.fixture['cases']],cases)
        for row,native in zip(self.fixture['cases'],validation.run(self.binary,cases)):
            with self.subTest(case=row['input']['id']):self.assertEqual(row['original'],native)

    def test_original_loop_and_source_provenance(self):
        report=json.loads((ROOT/'analysis/mqtt-client-validation.json').read_text());self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested']);self.assertFalse(report['complete_application_linked'])
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/mqtt-client-original.json.gz'))
        self.assertEqual(report['cases'],len(self.fixture['cases']))
        for address in ['0x40126d8c','0x40126f6c','0x40126b90','0x401270fc','0x4012718c','0x40126ccc']:
            self.assertGreater(report['original_functions_executed'][address],0)
        self.assertGreater(report['modeled_boundaries']['Xtensa_hardware_loop_backedge'],0)
        for path,sha in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/path),sha,path)
        target=json.loads((ROOT/'analysis/mqtt-client-target-build.json').read_text())
        for row in target['sources']:self.assertEqual(validation.digest(ROOT/row['source']),row['source_sha256'])

    def test_missing_services_and_unsafe_frame_guards(self):
        cases=[dict(id='missing',op=5,missing_read=True,state=0),dict(id='overlong-connect',op=2,connected=False,client_id='d'*1024),dict(id='bad-topic-span',op=5,state=0,incoming=[0x30,2,255,255])]
        rows=validation.run(self.binary,cases)
        self.assertEqual([r['result'] for r in rows],[1,4,5])
        self.assertFalse(rows[0]['effects'][0][0]=='write')
        self.assertFalse(any(e[0] in ['connect','write'] for e in rows[1]['effects']))
        self.assertFalse(any(e[0]=='message' for e in rows[2]['effects']))
        self.assertEqual(rows[1]['state'],0xffffffff)


if __name__=='__main__':unittest.main()
