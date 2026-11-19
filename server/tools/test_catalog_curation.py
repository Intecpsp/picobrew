"""Curated exclusions bind exact originals and preserve the source archive."""
import copy
import hashlib
import json
from pathlib import Path
import unittest
import zipfile

from catalog_curation import apply_reviewed_exclusions, load_policy


class CurationTests(unittest.TestCase):
    def test_review_matches_bundle_and_all_excluded_originals(self):
        policy = load_policy()
        bundle = Path(__file__).resolve().parents[1] / "internal/catalog/data/catalog.zip"
        with zipfile.ZipFile(bundle) as archive:
            self.assertEqual(json.loads(archive.read("curation.json")), policy)
            available = {entry["id"] for entry in json.loads(archive.read("index.json"))}
            for decision in policy["removed_entries"]:
                self.assertNotIn(decision["id"], available)
                self.assertNotIn("entries/" + decision["id"] + ".json", archive.namelist())
                for record in decision["source_records"]:
                    data = archive.read("originals/" + decision["source_id"] + "/" + record["path"])
                    self.assertEqual(len(data), record["bytes"])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), record["sha256"])

    def fixture(self):
        decision = copy.deepcopy(load_policy()["removed_entries"][0])
        source = {"id": decision["source_id"], "commit": "a" * 40,
                  "record_count": len(decision["source_records"]),
                  "valid_count": len(decision["source_records"]), "excluded_count": 0,
                  "excluded_reasons": {}, "entry_count": 1}
        entry = {"id": decision["id"], "name": decision["name"],
                 "source_id": decision["source_id"], "source_records": copy.deepcopy(decision["source_records"])}
        policy = {"source_commits": {source["id"]: source["commit"]}, "removed_entries": [decision]}
        return entry, source, policy

    def test_changed_original_requires_new_review_without_mutating_inputs(self):
        entry, source, policy = self.fixture()
        entry["source_records"][0]["sha256"] = "0" * 64
        before = copy.deepcopy((entry, source, policy))
        with self.assertRaisesRegex(ValueError, "original records changed"):
            apply_reviewed_exclusions([entry], [source], [], policy)
        self.assertEqual((entry, source, policy), before)

    def test_missing_identity_or_repeated_decision_does_not_silently_remove_data(self):
        entry, source, policy = self.fixture()
        with self.assertRaisesRegex(ValueError, "entry is missing"):
            apply_reviewed_exclusions([], [source], [], policy)
        policy["removed_entries"].append(copy.deepcopy(policy["removed_entries"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate curation"):
            apply_reviewed_exclusions([entry], [source], [], policy)


if __name__ == "__main__":
    unittest.main()
