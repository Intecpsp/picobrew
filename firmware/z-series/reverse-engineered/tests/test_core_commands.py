"""Portable acceptance of the original Core dispatcher and session metadata."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_core_commands as validation

class CoreCommandsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='zseries-core-command-test-')
        cls.addClassCleanup(cls.build.cleanup)
        cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads((ROOT/'tests/fixtures/core-commands-original.json').read_text())

    def test_original_dispatch_metadata_and_order(self):
        for item in self.fixture['cases']:
            with self.subTest(case=item['input']['id']):validation.compare_case(self.native,item['input'],item['original'])

    def test_resume_loaded_preserves_pending_command(self):
        for pending in (0,4):
            item=validation.case('queue',core=dict(state=12,command_id=51,queue_mask=pending),queued_program_id=99)
            status,result=validation.native_case(self.native,item)
            self.assertEqual(status,0)
            self.assertEqual(result['words'][48],4)
            self.assertEqual(result['words'][55],51 if pending else 13)
            self.assertEqual(result['extra'][21],99 if pending else 24)
            self.assertEqual(result['extra'][9],1)

    def test_missing_runtime_endpoint_is_reported(self):
        item=validation.case('missing-runtime',core=dict(state=14,command_id=34))
        status,result=validation.native_case(self.native,item,missing=True)
        self.assertEqual(status,1)
        self.assertEqual(result['words'],item['words'])
        self.assertEqual(result['events'],[])
        self.assertEqual(result['runtime_commands'],[])

    def test_runtime_payload_preserves_constructor_defaults(self):
        item=validation.case('duration',core=dict(state=14,command_id=34),duration=123456)
        status,result=validation.native_case(self.native,item)
        self.assertEqual(status,0)
        posted=result['runtime_commands'][0]
        self.assertEqual(posted[:6],[10,0,0,0,123456,0])
        self.assertEqual(posted[6:8],[0,1])
        self.assertEqual(posted[-1],0)

    def test_provenance(self):
        report=json.loads((ROOT/'analysis/core-commands-validation.json').read_text())
        self.assertEqual(report['input_sha256'],validation.common.digest(ROOT.parent/'oem/upstream/zseries_0_0_119.bin'))
        self.assertEqual(report['portable_golden_sha256'],validation.common.digest(ROOT/'tests/fixtures/core-commands-original.json'))
        self.assertEqual(report['portable_golden_cases'],len(self.fixture['cases']))
        for path,expected in report['source_sha256'].items():self.assertEqual(validation.common.digest(ROOT/path),expected,path)
        for path,expected in report['accepted_dependency_reports_sha256'].items():self.assertEqual(validation.common.digest(ROOT/'analysis'/path),expected,path)
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])

if __name__=='__main__':unittest.main()
