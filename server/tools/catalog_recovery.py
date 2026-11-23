"""Reconcile ingredient records against PicoBrew's pinned loading sheets and timed valve programs."""
import argparse
from collections import Counter
from copy import deepcopy
from hashlib import sha256
import json
from pathlib import Path
import re
from zipfile import ZipFile

from catalog_public import convert_public, _misc_amount
from catalog_author_review import load_reviews, reviewed_recovery

DATA = Path(__file__).resolve().parents[1] / "internal/catalog/data"

HOP_ERROR = re.compile(r"Boil hops with time of ([\d.]+) \( .+ \) exceeds limit of 1\.5 oz\.")


def recover_units(source):
    """Recover a missing unit only when the sheet supplies a non-default measure."""
    loading = source["HumanBrewingSteps"]
    if loading["IsMetric"] is not False:
        return False
    recovered = False
    for group in ("Adjuncts", "WhirlpoolAdjuncts", "DryAdjuncts"):
        for item in source[group]:
            if item.get("Units") or item["Amount"] <= 0:
                continue
            if group == "DryAdjuncts":
                contents = loading["FermentationAdditions"]
            else:
                valve = item["Location"]
                rounds = [r for r in loading["AdjunctRounds"] if r["Round"] == item["AdjunctRound"]]
                if not 2 <= valve <= 5 or len(rounds) != 1:
                    continue
                contents = rounds[0][f"Adjuncts{valve - 1}"]
            units = {i.get("Units") for i in contents
                     if (i["Name"], i["Amount"]) == (item["Name"], item["Amount"])}
            if len(units) == 1:
                unit = units.pop()
                # Beer Crafter prints oz for a blank US adjunct unit, including
                # tablets and spoon measures. That label is not independent evidence.
                if isinstance(unit, str) and unit.strip().lower() != "oz" and _misc_amount(item["Amount"], unit):
                    item["Units"] = unit
                    recovered = True
    return recovered


def matching_boil(source, items):
    """Check the first direct flow, countdown and loading list for each occupied cage."""
    loading = source["HumanBrewingSteps"]
    rounds = loading["AdjunctRounds"]
    if (source["IsBiB"] or loading["IsMetric"] is not False or loading["TotalRounds"] != 1
            or len(rounds) != 1 or rounds[0]["Round"] != 1
            or source["WhirlpoolHops"] or source["WhirlpoolAdjuncts"] or source["WhirlpoolSteps"]):
        return False
    if any(i["AdjunctRound"] != 1 or i["Errors"] or not 2 <= i["Location"] <= 5
           or i["Amount"] <= 0 or not 0 < i["Time"] <= source["BoilTime"] for i in items):
        return False
    for valve in range(2, 6):
        declared = Counter((i["Name"], i["Amount"], i.get("Units") or "oz")
                           for i in items if i["Location"] == valve)
        loaded = Counter((i["Name"], i["Amount"], i.get("Units"))
                         for i in rounds[0][f"Adjuncts{valve - 1}"])
        if declared != loaded:
            return False
    # Identify the original boil by its heat-up, then stop at the end of its
    # timed segments. Do not count a later chiller, pause or whirlpool as a boil.
    steps = source["MachineSteps"]
    temp = source["BoilTemp"]
    starts = [n for n, s in enumerate(steps) if s["Time"] == 0 and
              s["Temperature"] == temp and s["Name"].lower().startswith("heat to boil")]
    if len(starts) != 1 or not 185 <= temp <= 212:
        return False
    start = starts[0]
    if any(s["StepLocation"] >= 2 or (s["Time"] and s["StepLocation"] != 1) for s in steps[:start]):
        return False
    left = source["BoilTime"]
    first = {}
    last_valve = 0
    for s in steps[start:]:
        if left == 0:
            break
        if s["Temperature"] != temp or s["Time"] < 0 or s["StepLocation"] not in (0, 2, 3, 4, 5):
            return False
        if s["StepLocation"] >= 2:
            if s["StepLocation"] < last_valve:
                return False
            last_valve = s["StepLocation"]
            first.setdefault(last_valve, left)
        left -= s["Time"]
    used = {i["Location"] for i in items}
    if left != 0 or used != set(first) or any(first[i["Location"]] != i["Time"] for i in items):
        return False
    # Cascading hardware wets cages in sequence. A reroute must retain that order.
    if list(first) != list(range(2, 2 + len(first))) or len(set(first.values())) != len(first):
        return False
    ramp = steps[start]["StepLocation"]
    return ramp == (2 if source["IsFirstWort"] else 0)


def recovery(entry, raw):
    source = raw["VM"]["Recipe"]
    old = entry["recipe"]
    warnings = old["provenance"]["warnings"]
    if (source["IsMetric"] is not False or not source.get("HumanBrewingSteps")
            or not (source["Errors"] or source["Adjuncts"] or source["DryAdjuncts"] or source["WhirlpoolAdjuncts"] or source["IsFirstWort"]
                    or any(h["BoilUse"] for h in source["Hops"]))):
        return None
    working = deepcopy(raw)
    r = working["VM"]["Recipe"]
    items = r["Hops"] + r["Adjuncts"]
    units_recovered = recover_units(r)
    def quantities_only():
        if not units_recovered:
            return None
        converted, remaining = convert_public(working)
        resolved = [w for w in warnings if w.startswith("Some adjunct or water-amendment quantities")
                    and w not in remaining]
        return {"source_sha256": entry["source_records"][0]["sha256"],
                "ingredients": converted["ingredients"], "resolved": resolved,
                "review": "The loading sheet supplies additional ingredient units. The complete flow path or slot capacity still needs review."}

    if not matching_boil(r, items):
        return quantities_only()
    # Cage capacity needs a measured mass for each timed adjunct.
    for adjunct in r["Adjuncts"]:
        converted = _misc_amount(adjunct["Amount"], adjunct["Units"])
        if not converted or converted[1] != "g":
            return quantities_only()
        if not adjunct["Name"].strip() or "see notes" in adjunct["Name"].lower():
            return None
    batch_error = False
    for error in r["Errors"]:
        msg = error["Description"]
        match = HOP_ERROR.fullmatch(msg)
        if error["Type"] == 1 and match:
            mass = sum(i["Amount"] for i in r["Hops"] if i["Time"] == float(match[1]))
            if mass > 1.5:
                continue
        if error["Type"] == 10 and msg == "Batch size must be less than 3.5 Gallons." and r["BatchSize"] >= 3.5:
            batch_error = True
            continue
        return None
    if any(i.get("Errors") for coll in ("MashSteps", "Fermentables", "BoilSteps", "Amendments", "DryHops", "DryAdjuncts") for i in r[coll]):
        return None
    if r["IsFirstWort"] and (not any(h["Location"] == 2 for h in r["Hops"])
                             or any(i["Location"] == 2 for i in r["Adjuncts"])):
        return None
    for hop in r["Hops"]:
        hop["BoilUse"] = 0  # direct exposure agrees with the printed loading sheet and countdown
    r["IsFirstWort"] = False
    r["Errors"] = []
    corrected, remaining = convert_public(working)
    ingredients = corrected["ingredients"]
    if source["IsFirstWort"]:
        for h, i in zip(r["Hops"], (i for i in ingredients if i["kind"] == "hop" and i["use"] == "boil"), strict=True):
            if h["Location"] == 2:
                i["use"] = "first-wort"
    for i in ingredients:
        if i["kind"] == "misc" and i["use"] == "boil":
            i["slot_addition"] = True
    removed = [w for w in warnings if w not in remaining and w.startswith(
        ("The source marks first-wort", "Some main-hop records", "The original source contains errors",
         "Some adjunct or water-amendment quantities"))]
    if not removed and ingredients == old["ingredients"]:
        return None
    result = {"source_sha256": entry["source_records"][0]["sha256"], "resolved": removed}
    if ingredients != old["ingredients"]:
        result["ingredients"] = ingredients
    steps = r["MachineSteps"]
    start = next(n for n, s in enumerate(steps) if s["Name"].lower().startswith("heat to boil")
                 and s["Temperature"] == r["BoilTemp"] and s["Time"] == 0)
    mash = [(s["Temperature"], s["Time"]) for s in steps[:start] if s["Time"]]
    if mash != [(s["Temp"], s["Time"]) for s in r["MashSteps"]]:
        result["review"] = "The source mash program and declared mash rests disagree."
    left = r["BoilTime"]
    end = start
    while left > 0:
        left -= steps[end]["Time"]
        end += 1
    if any(s["StepLocation"] not in (0, 6) or "chill" not in s["Name"].lower() for s in steps[end:]):
        result["review"] = "The source program has an additional hot-side operation after the declared boil."
    if batch_error:
        result["batch_limit"] = True
    return result


def generate():
    reviews = json.loads(Path(__file__).with_name("catalog-recovery-reviews.json").read_bytes())
    author_reviews = load_reviews()
    matched = set()
    author_matched = set()
    records = {}
    with ZipFile(DATA / "catalog.zip") as archive:
        for path in sorted(archive.namelist()):
            if not path.startswith("entries/public-"):
                continue
            entry = json.loads(archive.read(path))
            record = entry["source_records"][0]
            data = archive.read("originals/public-recipes/" + record["path"])
            if sha256(data).hexdigest() != record["sha256"]:
                raise ValueError("Archive checksum mismatch: " + path)
            raw = json.loads(data)
            source = raw["VM"]["Recipe"]
            if entry["id"] in author_reviews:
                try:
                    records[entry["id"]] = reviewed_recovery(
                        entry, raw, author_reviews[entry["id"]], reviews.get(entry["id"]),
                        recovery, recover_units)
                except (ValueError, KeyError, IndexError) as error:
                    raise ValueError(f"{entry['name']} ({entry['id']}): {error}") from error
                author_matched.add(entry["id"])
                if entry["id"] in reviews:
                    matched.add(entry["id"])
                continue
            result = recovery(entry, raw)
            review = reviews.get(entry["id"])
            if review:
                if review["source_sha256"] != record["sha256"]:
                    raise ValueError("Changed recipe requires a new author-note review: " + path)
                if not result:
                    if not review.get("blocker"):
                        raise ValueError("Author review lacks a matched program: " + path)
                    result = {"source_sha256": record["sha256"], "resolved": []}
                if review.get("blocker"):
                    result["review"] = review["blocker"]
                matched.add(entry["id"])
                if additions := review.get("additions"):
                    ingredients = result.setdefault("ingredients", list(entry["recipe"]["ingredients"]))
                    ingredients.extend(additions)
            elif (source.get("Notes") or "").strip():
                continue
            if result and any("see notes" in i["name"].lower() or not i["name"].strip(' "\'')
                              for i in result.get("ingredients", entry["recipe"]["ingredients"])):
                continue
            if result:
                records[entry["id"]] = result
    if matched != reviews.keys() or author_matched != author_reviews.keys():
        raise ValueError("Missing source for an author-note review")
    return records


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--check", action="store_true")
    args = cli.parse_args()
    records = generate()
    data = json.dumps(records, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    dest = DATA / "recovered-recipes.json"
    if args.check:
        if dest.read_text(encoding="utf-8") != data:
            raise SystemExit("Recipe recovery differs from the pinned sources")
    else:
        dest.write_text(data, encoding="utf-8")
    print(f"{len(records)} matched source records")
