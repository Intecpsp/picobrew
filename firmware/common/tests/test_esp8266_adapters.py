#!/usr/bin/env python3
import json
import os
from pathlib import Path
import tempfile
import unittest
import esp8266_adapters_native as native

class AdapterEvidenceTests(unittest.TestCase):
    def test_evidence_matches_owned_sources(self):
        report=json.loads((native.ROOT/'analysis/esp8266-adapters-validation.json').read_text())
        for name,expected in report['source_sha256'].items():
            self.assertEqual(native.digest(native.ROOT/name),expected,name)
        self.assertTrue(report['results']['storage']['passed'])
        self.assertTrue(report['results']['telemetry']['passed'])

class Esp8266AdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        value=os.environ.get('PICO_ESP8266_SDK')
        if not value:raise unittest.SkipTest('Set PICO_ESP8266_SDK to the pinned Arduino ESP8266 2.4.1 tree')
        cls.sdk=Path(value);native.verify_sdk(cls.sdk)
        cls.i386=os.environ.get('PICO_I386_TOOLCHAIN')
    def run_suite(self,kind):
        with tempfile.TemporaryDirectory(prefix='picobrew-esp8266-'+kind+'-') as directory:
            return native.build_and_run(directory,self.sdk,kind,self.i386)
    def test_actual_spiffs_mount_persistence_and_errors(self):
        report=self.run_suite('storage');self.assertTrue(report['passed'])
        self.assertGreater(report['actual_spiffs_hal_reads'],1000)
        self.assertGreater(report['checks'],60)
    def test_async_dns_tcp_backpressure_and_lifetimes(self):
        report=self.run_suite('telemetry');self.assertTrue(report['passed'])
        self.assertGreater(report['checks'],250)
        self.assertEqual(report['pbuf_nodes_created'],report['pbuf_nodes_freed'])

if __name__=='__main__':unittest.main()
