import gzip
import json
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import validate_local_http_application as validation


class LocalHttpApplicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="z-http-application-test-")
        cls.binary = validation.native.build(cls.directory.name)
        cls.fixture = json.loads(gzip.decompress(validation.FIXTURE.read_bytes()))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_original_post_recipe_and_full_mailbox_composition(self):
        cases = validation.inputs.cases()
        self.assertEqual([value["input"] for value in self.fixture["cases"]], cases)
        actual = validation.native.run(self.binary, cases)
        exact = repaired = 0
        for case, original, result in zip(cases, self.fixture["cases"], actual):
            with self.subTest(case=case["id"]):
                self.assertEqual(result, validation.expected(case, original["original"]))
                if result == validation.application_snapshot(original["original"]): exact += 1
                else: repaired += 1
        self.assertEqual((exact, repaired), (62, 6))

    def test_original_unnamed_recipes_continue_after_returning_assertions(self):
        observed = 0
        for value in self.fixture["cases"]:
            case, original = value["input"], value["original"]
            if original["assertion_lines"]:
                with self.subTest(case=case["id"]):
                    self.assertEqual(original["assertion_lines"], [339, 626])
                    self.assertEqual(original["assertion_counter"], 2)
                    self.assertEqual(original["assertion_delays"], [200, 200])
                    self.assertEqual((original["status"], original["length"], original["mime"], original["body"]), (200, 0, None, ""))
                    self.assertEqual(original["buffer"], dict(type=15, id=0, name="", code=[[57, 256], [40, 0]], strings=[""]))
                    self.assertEqual(original["parameters"]["water"], ["4026b851eb851eb8", 0])
                    self.assertEqual(original["stats"]["duration"], 60)
                    self.assertEqual(struct.unpack_from("<I", bytes.fromhex(original["current_hex"]))[0], 7)
                    self.assertTrue(original["pending"])
                    if case["pending"]:
                        self.assertEqual(original["queued_hex"], case["queued_hex"][8:])
                    else:
                        self.assertEqual(original["queued_hex"], original["current_hex"])
                observed += 1
        self.assertEqual(observed, 6)

    def test_request_fragments_live_state_owned_program_and_failures(self):
        binary = validation.native.build(self.directory.name, source="local_http_application_integration.cpp")
        subprocess.run([str(binary)], check=True, timeout=10)

    def test_original_bytes_source_evidence_and_target_adapter(self):
        validation.verify_original_ranges(self.fixture)
        report = json.loads(validation.REPORT.read_text())
        self.assertTrue(report["all_compatibility_expectations_pass"])
        self.assertEqual((report["exact_matches"], report["intentional_bounded_rejections"]), (62, 6))
        self.assertFalse(report["hardware_tested"])
        self.assertEqual(report["golden_fixture_sha256"], validation.digest(validation.FIXTURE))
        for path, identity in report["source_sha256"].items():
            self.assertEqual(identity, validation.digest(ROOT / path), path)
        target = json.loads(validation.TARGET.read_text())
        self.assertFalse(target["complete_application_linked"])
        self.assertFalse(target["hardware_tested"])
        self.assertEqual(len(target["sources"]), 2)
        for source in target["sources"]:
            self.assertEqual(source["source_sha256"], validation.digest(ROOT / source["source"]))
        for path, identity in {**target["owned_header_sha256"], **target["platform_header_sha256"]}.items():
            self.assertEqual(identity, validation.digest(ROOT / path), path)


if __name__ == "__main__":
    unittest.main()
