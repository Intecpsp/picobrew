import json
import unittest
from zipfile import ZipFile

from catalog_recovery import DATA, generate, recovery


class RecoveryTests(unittest.TestCase):
    def test_bundled_index_matches_pinned_sources_and_reviews(self):
        self.assertEqual(json.loads((DATA / "recovered-recipes.json").read_bytes()), generate())

    def test_unknown_error_and_shifted_hop_time_keep_capacity_record_blocked(self):
        with ZipFile(DATA / "catalog.zip") as archive:
            index = json.loads(archive.read("index.json"))
            entry_id = next(e["id"] for e in index if e["name"] == "Amarillo Pale" and e["kind"] == "recipe")
            entry = json.loads(archive.read("entries/" + entry_id + ".json"))
            raw = json.loads(archive.read("originals/public-recipes/" + entry["source_records"][0]["path"]))
        source = raw["VM"]["Recipe"]
        self.assertTrue(recovery(entry, raw)["resolved"])
        source["Errors"].append({"Type": 1, "Description": "Unverified addition"})
        self.assertIsNone(recovery(entry, raw))
        source["Errors"].pop()
        source["Hops"][0]["Time"] -= 1
        self.assertIsNone(recovery(entry, raw))
