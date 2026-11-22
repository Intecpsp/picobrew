import copy
import json
import unittest
from zipfile import ZipFile

from catalog_brew_sheets import DATA, brew_sheet, generate


class BrewSheetTests(unittest.TestCase):
    def test_index_agrees_with_pinned_sources(self):
        self.assertEqual((DATA / "brew-sheets.json").read_text(encoding="utf-8"), generate())

    def test_contradictory_sources_stay_in_archive(self):
        with ZipFile(DATA / "catalog.zip") as archive:
            key = next(iter(json.loads((DATA / "brew-sheets.json").read_bytes())))
            entry = json.loads(archive.read(f"entries/{key}.json"))
            raw = json.loads(archive.read("originals/public-recipes/" + entry["source_records"][0]["path"]))
        self.assertTrue(brew_sheet(entry, raw))
        def alter_route(r):
            next(s for s in r["MachineSteps"] if s["StepLocation"] == 2)["StepLocation"] = 3
        def alter_weight(r):
            r["HumanBrewingSteps"]["Mash"][0]["Amount"] += 1
        def alter_time(r):
            r["Hops"][0]["Time"] += 1
        def remove_yeast(r):
            r["Yeast"] = None
        for change in (alter_route, alter_weight, alter_time, remove_yeast):
            with self.subTest(change=change.__name__):
                changed = copy.deepcopy(raw)
                change(changed["VM"]["Recipe"])
                self.assertIsNone(brew_sheet(entry, changed))
