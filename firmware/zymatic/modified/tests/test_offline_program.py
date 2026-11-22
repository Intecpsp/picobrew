import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
import hashlib

MODIFIED = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MODIFIED / "tools"))
import offline_program as program


def sample(name="Test", count=1):
    return {"model": "zymatic", "name": name, "steps": [
        {"name": f"Step {i+1}", "target_f": 152, "minutes": 12, "location": 1, "drain_minutes": 3}
        for i in range(count)]}


class OfflineProgram(unittest.TestCase):
    def test_user_program_roundtrip_and_content_id(self):
        user = sample(count=3)
        # Archived Party Porter and other OEM schedules retain unused values
        # on a nonterminal physical pause. Preserve all fields verbatim.
        user["steps"][1].update(location=6, target_f=152, minutes=1, drain_minutes=0)
        packed = program.pack_program(user)
        decoded = program.unpack_program(packed)
        self.assertEqual(decoded["steps"], user["steps"])
        self.assertEqual(decoded["name"], user["name"])
        self.assertEqual(len(packed), 516)
        self.assertEqual(program.pack_program(decoded), packed)
        user["steps"][2]["target_f"] += 1
        self.assertNotEqual(program.unpack_program(program.pack_program(user))["id"], decoded["id"])

    def test_documented_default_matches_pinned_archive(self):
        source = "originals/chiefwigms/app/static/support/base_recipes/zymatic/Base_Recipe_-_American_Brown_Ale.json"
        with zipfile.ZipFile(MODIFIED.parents[2] / "server/internal/catalog/data/catalog.zip") as archive:
            original = archive.read(source)
        self.assertEqual(hashlib.sha256(original).hexdigest(), "cc7f7047d96443d04dfcda7e08d50b6948d7e7043eba3b87406186d1e07a9ab2")
        locations = ["PassThru", "Mash", "Adjunct1", "Adjunct2", "Adjunct3", "Adjunct4", "Pause"]
        steps = json.loads(original)["steps"]
        decoded = program.unpack_program(program.pack_program())
        self.assertEqual(decoded["steps"], [
            dict(name=s["name"], target_f=s["temperature"], minutes=s["step_time"],
                 location=locations.index(s["location"]), drain_minutes=s["drain_time"])
            for s in steps])

    def test_machine_limits_and_delimiters(self):
        bad = []
        for key, value in (("target_f", 208), ("minutes", -1), ("drain_minutes", 256), ("location", 7),
                           ("minutes", True), ("target_f", "150"), ("name", "a/b"), ("name", "A"*21)):
            s = sample(); s["steps"][0][key] = value; bad.append(s)
        for s in (sample(count=0), sample(count=26), sample("recipe#other"), sample("Ü"), sample(" ")):
            bad.append(s)
        s = sample(); s["steps"][0].update(location=6, target_f=0, minutes=0, drain_minutes=0); bad.append(s)
        s = sample(); s["model"] = "pico-s"; bad.append(s)
        s = sample(); s["z_program"] = {"type":"Temperature"}; bad.append(s)
        for s in bad:
            with self.subTest(s=s), self.assertRaises(ValueError):
                program.pack_program(s)

    def test_exact_wire_size_and_max_steps(self):
        s = sample(count=25)
        for step in s["steps"]: step["name"] = "A"
        # Expand step names one byte at a time to force the exact raw limit.
        while int.from_bytes(program.pack_program(s)[1:3], "little") < 511:
            step = next(step for step in s["steps"] if len(step["name"]) < 20)
            step["name"] += "A"
        b = program.pack_program(s)
        self.assertEqual(program.unpack_program(b)["steps"], s["steps"])
        next(step for step in s["steps"] if len(step["name"]) < 20)["name"] += "A"
        with self.assertRaises(ValueError): program.pack_program(s)

    def test_bad_header_noncanonical_padding_and_numeric_fields(self):
        good = program.pack_program(sample())
        for index in (0, 1, 2, 3, 4 + len("Test"), len(good) - 1):
            bad = bytearray(good); bad[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                program.unpack_program(bytes(bad))
        for broken in (good.replace(b"152", b"999"), good.replace(b"152", b"009"),
                       good.replace(b"152", b"-10"), bytes(516), good[:-1]):
            with self.assertRaises(ValueError): program.unpack_program(broken)

    def test_import_requires_explicit_wire_model(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"recipe.json"
            s = sample(); path.write_text(json.dumps(s))
            self.assertEqual(program.read_program(path)["name"], "Test")
            s.pop("model"); path.write_text(json.dumps(s))
            with self.assertRaises(ValueError): program.read_program(path)
