"""Build the offline brew-sheet index from the pinned PicoBrew public recipe archive.

Accept single-load all-grain recipes when the ingredient bill, loading sheet,
and actual stock machine routing agree. Keep all other originals in the archive.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile

DATA = Path(__file__).resolve().parents[1] / "internal/catalog/data"
PITCH_GUIDES = {
    # Technical sheet: product slug, grams per liter, primary temperature range (°C).
    "US-05": ("safale-us-05", 0.5, 0.8, 18, 26),
    "S-04": ("safale-s-04", 0.5, 0.8, 18, 26),
    "S-33": ("safale-s-33", 0.5, 0.8, 18, 26),
    "T-58": ("safale-t-58", 0.5, 0.8, 18, 26),
    "S-23": ("saflager-s-23", 0.8, 1.2, 12, 18),
    "W-34/70": ("saflager-w-34-70", 0.8, 1.2, 12, 18),
}


def weighed(items, unit):
    return Counter((i["Name"], i["Amount"], i.get("Units", unit)) for i in items)


def brew_sheet(entry, raw):
    """Return a brew sheet only where every required comparison succeeds."""
    r = raw["VM"]["Recipe"]
    loading = r.get("HumanBrewingSteps") or {}
    yeast = r.get("Yeast") or {}
    guide = PITCH_GUIDES.get(yeast.get("ProductID"))
    if not guide or yeast.get("Laboratory") != "Fermentis":
        return
    if (r["GUID"] != entry["id"].removeprefix("public-") or not entry["can_copy"]
            or r["IsMetric"] or loading.get("IsMetric") is not False or r["IsBiB"]
            or r["IsFirstWort"] or r["Errors"] or r["Warnings"]
            or any(r[k] for k in ("Amendments", "Adjuncts", "WhirlpoolHops", "DryHops",
                                   "WhirlpoolAdjuncts", "DryAdjuncts", "WhirlpoolSteps"))):
        return
    grains, hops = r["Fermentables"], r["Hops"]
    if not grains or not hops or not r["MashSteps"] or not 0 < r["BatchSize"] <= 2.5:
        return
    if not r["H2O"] or not r["BatchSize"] <= r["H2O"] <= 5 or not r["BoilTime"]:
        return
    if any(not i["Name"].strip() or i["Amount"] <= 0 or i["Errors"] for i in grains + hops):
        return
    if any(i["FermentableType"] not in ("Grain", "OtherGrain") for i in grains):
        return
    if weighed(loading.get("Mash", []), "lbs") != weighed(grains, "lbs"):
        return
    rounds = loading.get("AdjunctRounds") or []
    if (loading.get("TotalRounds") != 1 or len(rounds) != 1 or rounds[0]["Round"] != 1
            or loading.get("FermentationAdditions")):
        return
    if any(h["BoilUse"] != 0 or h["AdjunctRound"] != 1 or not 2 <= h["Location"] <= 5
           or not 0 < h["Time"] <= r["BoilTime"] for h in hops):
        return
    slots = []
    for slot in range(1, 5):
        items = rounds[0][f"Adjuncts{slot}"]
        expected = [h for h in hops if h["Location"] == slot + 1]
        if weighed(items, "oz") != weighed(expected, "oz"):
            return
        for h in expected:
            slots.append(f"Slot {slot}: {h['Amount']:g} oz {h['Name']} ({h['Time']:g} min before boil ends)")

    steps = r["MachineSteps"] or []
    if not 1 <= len(steps) <= 25 or steps[0]["Step"] not in (0, 1):
        return
    for index, s in enumerate(steps, steps[0]["Step"]):
        if (s["ID"] != r["RecipeID"] or s["Step"] != index or not s["Name"].strip()
                or not all(type(s[k]) is int for k in ("StepLocation", "Temperature", "Time", "Drain"))
                or not 0 <= s["StepLocation"] <= 6 or not 0 <= s["Temperature"] <= 207
                or not 0 <= s["Time"] <= 255 or not 0 <= s["Drain"] <= 255):
            return
    mash = [(s["Temp"], s["Time"]) for s in r["MashSteps"]]
    machine_mash = [(s["Temperature"], s["Time"]) for s in steps if s["StepLocation"] == 1 and s["Time"]]
    if mash != machine_mash:
        return
    # Work backward from the end of the boil and verify the first exposure of
    # each loaded cage at the temperature and countdown printed for its hops.
    boil_temp = r["BoilTemp"]
    if not 185 <= boil_temp <= 207:
        return
    boil = [s for s in steps if s["Temperature"] == boil_temp and s["Time"] > 0]
    if sum(s["Time"] for s in boil) != r["BoilTime"]:
        return
    left = r["BoilTime"]
    exposed = {}
    for s in boil:
        exposed.setdefault(s["StepLocation"], left)
        left -= s["Time"]
    if any(exposed.get(h["Location"]) != h["Time"] for h in hops):
        return
    if any(s["StepLocation"] >= 2 and s["StepLocation"] <= 5 and s["Temperature"] != boil_temp
           for s in steps):
        return
    # A gap before a loaded cage also wets it when water cascades from above.
    cages = [s["StepLocation"] for s in boil if s["StepLocation"] >= 2]
    if cages != sorted(cages) or set(cages) != {h["Location"] for h in hops}:
        return
    phases = entry["recipe"]["fermentation"]
    slug, low_dose, high_dose, low_temp, high_temp = guide
    if not phases or not low_temp <= phases[0]["target_c"] <= high_temp:
        return
    if sum(i["Amount"] for i in grains) > 9:
        return
    by_cage = defaultdict(float)
    for h in hops:
        by_cage[h["Location"]] += h["Amount"] * 28.349523125
    if max(by_cage.values()) > 42.5:
        return
    liters = entry["recipe"]["batch_liters"]
    instructions = [f"Fill the keg with {r['H2O']:g} US gal of water.",
                    "Mill and load the mash compartment: " + ", ".join(
                        f"{g['Amount']:g} lb {g['Name']}" for g in grains) + ".", *slots,
                    f"Fermentis {yeast['ProductID']}: pitch {liters * low_dose:.2f}–{liters * high_dose:.2f} g for {liters:.2f} L (manufacturer’s dosage)."]
    instructions.extend(f"{p['name']}: {p['target_c']:.1f} °C for {p['days']:g} days." for p in phases)
    return dict(instructions=instructions, yeast_guide=f"https://fermentis.com/en/product/{slug}/", original_gravity=r.get("OG"),
                final_gravity=r.get("FG"), source_sha256=entry["source_records"][0]["sha256"])


def generate():
    sheets = {}
    with ZipFile(DATA / "catalog.zip") as archive:
        for path in sorted(archive.namelist()):
            if not path.startswith("entries/public-"):
                continue
            entry = json.loads(archive.read(path))
            record = entry["source_records"][0]
            data = archive.read("originals/public-recipes/" + record["path"])
            if hashlib.sha256(data).hexdigest() != record["sha256"]:
                raise ValueError(f"Source hash mismatch for {path}")
            sheet = brew_sheet(entry, json.loads(data))
            if sheet:
                sheets[entry["id"]] = sheet
    return json.dumps(sheets, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--check", action="store_true")
    args = cli.parse_args()
    output = DATA / "brew-sheets.json"
    generated = generate()
    if args.check:
        if output.read_text(encoding="utf-8") != generated:
            raise SystemExit("Bundled brew sheets differ from their sources.")
    else:
        output.write_text(generated, encoding="utf-8", newline="\n")
    print(f"{len(json.loads(generated))} source-matched Zymatic brew sheets")
