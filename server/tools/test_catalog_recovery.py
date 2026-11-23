from copy import deepcopy
from hashlib import sha256
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from catalog_author_review import load_reviews, reviewed_recovery
from catalog_recovery import DATA, generate, recover_units, recovery


def archived_recipe(entry_id):
    with ZipFile(DATA / "catalog.zip") as archive:
        entry = json.loads(archive.read(f"entries/{entry_id}.json"))
        path = entry["source_records"][0]["path"]
        raw = json.loads(archive.read(f"originals/public-recipes/{path}"))
    return entry, raw


class RecoveryTests(unittest.TestCase):
    def test_bundled_index_matches_pinned_sources_and_reviews(self):
        self.assertEqual(json.loads((DATA / "recovered-recipes.json").read_bytes()), generate())

    def test_review_covers_original_440_recipe_ids(self):
        # Sorted IDs from the 440 planning-only records in the original review queue.
        ids = sorted(load_reviews())
        self.assertEqual(len(ids), 440)
        self.assertEqual(sha256("\n".join(ids).encode()).hexdigest(),
                         "dfb74c5b24042eea188a47a28f048ca109fd82e27cf63fe5bc4050447f25a71f")

    def test_default_printed_ounce_does_not_supply_an_ingredient_weight(self):
        entry, raw = archived_recipe("public-19d6e5c595b04bdeba536e6761e193b2")  # Heinielicious
        source = raw["VM"]["Recipe"]
        moss, = source["Adjuncts"]
        printed, = [i for i in source["HumanBrewingSteps"]["AdjunctRounds"][0]["Adjuncts2"]
                    if i["Name"] == moss["Name"]]
        self.assertIsNone(moss["Units"])
        self.assertEqual((printed["Amount"], printed["Units"]), (moss["Amount"], "oz"))
        self.assertFalse(recover_units(source))
        self.assertIsNone(moss["Units"])
        review = load_reviews()[entry["id"]]
        result = reviewed_recovery(entry, raw, review, None, recovery, recover_units)
        self.assertFalse(any(i["name"] == moss["Name"] for i in result["ingredients"]))
        self.assertIn("Irish moss", result["review"])

    def test_author_teaspoon_overrides_printed_ounces_and_preserves_archived_recipe(self):
        entry, raw = archived_recipe("public-15e287774d754dec85e993b595a63df1")  # Patersbier v4
        original = deepcopy(raw)
        self.assertEqual(raw["VM"]["Recipe"]["Notes"].strip(), "1tsp irish moss")
        review = load_reviews()[entry["id"]]
        result = reviewed_recovery(entry, raw, review, None, recovery, recover_units)
        moss, = [i for i in result["ingredients"] if i["name"] == "Irish Moss"]
        self.assertAlmostEqual(moss["amount"], 4.92892159375)
        self.assertEqual((moss["unit"], moss["minutes"]), ("mL", 10))
        self.assertFalse(moss.get("slot_addition"))
        self.assertTrue(result["review"])
        self.assertEqual(raw, original)

    def test_author_weighed_spices_replace_placeholder_hop_once(self):
        entry, raw = archived_recipe("public-5bb105fbc8fa4ebfb93b5ae04d682979")  # Gilligan's Island
        result = reviewed_recovery(entry, raw, load_reviews()[entry["id"]], None, recovery, recover_units)
        late_spices = [i for i in result["ingredients"] if i["minutes"] == 5 and i["use"] == "boil"]
        self.assertCountEqual([i["name"] for i in late_spices],
                              ["Lime zest", "Sweet orange peel", "Ginger Root", "Coriander Seed"])
        self.assertTrue(all(i["unit"] == "g" and i["slot_addition"] for i in late_spices))
        coriander, = [i for i in late_spices if i["name"] == "Coriander Seed"]
        self.assertAlmostEqual(coriander["amount"], 14.1747615625)
        self.assertNotIn("review", result)

    def test_rejects_stale_review_digest_and_stale_source_edit(self):
        entry, raw = archived_recipe("public-15e287774d754dec85e993b595a63df1")
        review = load_reviews()[entry["id"]]
        with self.assertRaisesRegex(ValueError, "checksum"):
            reviewed_recovery(entry, raw, {**review, "source_sha256": "0" * 64}, None, recovery, recover_units)
        raw["VM"]["Recipe"]["Adjuncts"][0]["Amount"] = 9
        with self.assertRaisesRegex(ValueError, "/Adjuncts/0/Amount"):
            reviewed_recovery(entry, raw, review, None, recovery, recover_units)

    def test_rejects_recipe_id_repeated_across_review_files(self):
        with TemporaryDirectory() as temp:
            directory = Path(temp) / "catalog-author-reviews"
            directory.mkdir()
            for name in ("first.json", "second.json"):
                (directory / name).write_text('{"public-one": {}}', encoding="utf-8")
            with patch("catalog_author_review.__file__", str(Path(temp) / "catalog_author_review.py")):
                with self.assertRaisesRegex(ValueError, "Repeated author review"):
                    load_reviews()

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
