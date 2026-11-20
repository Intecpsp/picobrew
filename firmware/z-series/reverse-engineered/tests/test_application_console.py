"""Original Core registries and callback boundaries, plus composed owner contracts."""
from pathlib import Path
import gzip
import hashlib
import json
import os
import tempfile
import unittest
import sys
from application_console_cases import command_cases
from application_console_native import ROOT,build,run,project,registry_tree

def fixture(name):
    return json.loads(gzip.decompress((ROOT/'tests/fixtures'/f'application-console-{name}.json.gz').read_bytes()))
sys.path.insert(0,str(ROOT/'tools'))


class ApplicationConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory(prefix='application-console-');cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary=Path(os.environ['APPLICATION_CONSOLE_BINARY'])if os.environ.get('APPLICATION_CONSOLE_BINARY')else build(cls.temporary.name)

    def test_all_original_shells_entries_and_registration_order(self):
        value=run(self.binary,[dict(op='registry')])[0]
        self.assertTrue(value['registered']);self.assertTrue(value['idempotent']);self.assertEqual(value['effects'],[])
        expected=registry_tree(fixture('registry-original'))
        self.assertEqual(sorted(value['shells'],key=lambda s:s['name']),expected)
        self.assertEqual(len(expected),23);self.assertEqual(sum(len(s['entries'])for s in expected),252)

    def test_original_callbacks_real_returning_assertions_and_payload_bits(self):
        cases=command_cases();self.assertEqual(cases,fixture('command-inputs'))
        original=fixture('command-original');native=run(self.binary,cases)
        self.assertEqual(len(cases),297);self.assertEqual(len(original),len(cases))
        for case,expected,actual in zip(cases,original,native):
            with self.subTest(case=case['id']):
                self.assertTrue(actual['registered']);self.assertEqual(actual['result'],0)
                self.assertEqual(project(case,actual),expected)
        self.assertEqual(sum(r['counter']-7 for r in original),25)

    def test_stop_pause_act_before_enqueue_and_retention(self):
        cases=[dict(command='r '+token,started=True,automatic=True,duty=90,occupied=occupied)for token in ('stop','pause')for occupied in (False,True)]
        for case,value in zip(cases,run(self.binary,cases)):
            with self.subTest(case=case):
                self.assertEqual(value['result'],0);self.assertFalse(value['state']['heater'][0]);self.assertFalse(value['state']['heater'][2]);self.assertEqual(value['state']['heater'][3],0)
                tx=[e for e in value['effects']if e[0]=='msp_tx'];self.assertEqual(len(tx),1);self.assertEqual(tx[0][2],[8,2,13,64,173,222])
                self.assertEqual(value['state']['runtime'][1],10 if case['occupied']else 2 if case['command'].endswith('stop')else 3)

    def test_exact_setduty_dispatch_preserves_oem_registry_quirk(self):
        reject,accept=run(self.binary,[dict(command='x setduty 75',automatic=True,duty=33),dict(command='x setduty',automatic=True,duty=33)])
        self.assertEqual(reject['state']['heater'][3],33);self.assertEqual(reject['state']['counter'],7)
        self.assertTrue(any(e[0]=='log'and 'Failed to parse command' in e[-1]for e in reject['effects']))
        self.assertEqual(accept['state']['heater'][3],0);self.assertEqual(accept['state']['counter'],8);self.assertEqual(accept['state']['clock'],1200)

    def test_msp_boot_version_prints_nonzero_response_after_returning_assertion(self):
        cases=[dict(direct_shell='MSP',direct_token='bv',direct_argument='',msp_state=state,bsl_response=value)for state in(1,2)for value in(0,0xa1,0xff)]
        for case,result in zip(cases,run(self.binary,cases)):
            with self.subTest(case=case):
                self.assertEqual(result['result'],0)
                self.assertIn(['write',f"RESPONSE 0x{case['bsl_response']:X}\r\n"],result['effects'])
                self.assertEqual(result['state']['counter'],7 if case['msp_state']==1 else 8)

    def test_every_external_callback_requires_and_forwards_its_binding(self):
        callbacks=json.loads((ROOT/'analysis/application-console-callbacks.json').read_text())['callbacks'];external=[c for c in callbacks if c['owner']=='platform_or_head']
        cases=[dict(direct_shell=c['shell'],direct_token=c['token'],direct_argument='probe',missing_platform=missing)for missing in(False,True)for c in external]
        for case,result in zip(cases,run(self.binary,cases)):
            self.assertEqual(result['result'],3 if case['missing_platform']else 0)
            if case['missing_platform']:self.assertEqual(result['effects'],[])
        forwarded=run(self.binary,cases[:len(external)])
        for callback,result in zip(external,forwarded):self.assertEqual(result['effects'],[['platform',int(callback['callback'],16),'probe']])
        self.assertEqual(sum(c['owner']=='pending_diagnostic_self_test'for c in callbacks),0)

    def test_basic_original_body_and_registered_wrapper(self):
        from validate_application_console import validate_basic_dispatch
        self.assertEqual(validate_basic_dispatch(self.binary),2)

    def test_network_integration_all_original_bodies_and_registered_wrappers(self):
        from validate_application_console import validate_network_dispatch
        self.assertEqual(validate_network_dispatch(self.binary),13)

    def test_full_software_diagnostics_run_from_real_registry_without_platform_fallback(self):
        cases=[dict(direct_shell=s,direct_token=t,direct_argument='',missing_platform=True)for s,t in [('Runtime','t'),('PicoStill','test'),('PicoStill','t b')]]
        original=json.loads(gzip.decompress((ROOT/'tests/fixtures/software-console-original.json.gz').read_bytes()))['cases']
        for actual,index in zip(run(self.binary,cases),[0,10,12]):
            expected=original[index];effects=[]
            for event in actual['effects']:
                if event[0]=='scope':event[1]=int(event[1])
                effects.append('|'.join(str(value)for value in event))
            self.assertTrue(actual['registered']);self.assertEqual(actual['result'],0)
            self.assertEqual([actual['state']['counter'],actual['state']['clock']],expected['state'])
            self.assertEqual(effects,expected['events'])

    def test_evidence_matches_owned_sources_and_fixtures(self):
        report=json.loads((ROOT/'analysis/application-console-validation.json').read_text())
        self.assertTrue(report['all_results_match'])
        for name,expected in report['source_sha256'].items():
            with self.subTest(path=name):self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),expected)

if __name__=='__main__':unittest.main()
