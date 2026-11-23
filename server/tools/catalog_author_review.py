"""Apply checksum-pinned readings of the public recipes to editable copies."""

from copy import deepcopy
import json
from pathlib import Path

from catalog_public import convert_public


def unique_keys(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            raise ValueError(f"Duplicate review key: {key}")
        obj[key] = value
    return obj


def load_reviews():
    reviews = {}
    for path in sorted(Path(__file__).with_name("catalog-author-reviews").glob("*.json")):
        cohort = json.loads(path.read_bytes(), object_pairs_hook=unique_keys)
        if reviews.keys() & cohort.keys():
            raise ValueError(f"Repeated author review in {path.name}")
        reviews.update(cohort)
    return reviews


def edit_source(source, edit):
    """Apply a reviewed change only when the archived value still matches."""
    parts = edit["path"].split("/")
    if parts.pop(0) or not parts or not parts[-1]:
        raise ValueError(f"Invalid source path: {edit['path']}")
    target = source
    for i, token in enumerate(parts):
        key = token.replace("~1", "/").replace("~0", "~")
        if isinstance(target, list):
            if not key.isdigit():
                raise ValueError(f"Invalid array index: {edit['path']}")
            key = int(key)
        if i < len(parts) - 1:
            target = target[key]
            continue
        if target[key] != edit["from"]:
            raise ValueError(f"Archived value changed: {edit['path']}")
        if edit.get("remove") is True:
            del target[key]
        else:
            target[key] = deepcopy(edit["to"])


def select_ingredient(ingredients, selector):
    matches = [i for i in ingredients if all(i.get(k) == v for k, v in selector.items())]
    if len(matches) != 1:
        raise ValueError(f"Expected one ingredient matching {selector}, found {len(matches)}")
    return matches[0]


def reviewed_recovery(entry, raw, review, previous, recovery, recover_units):
    allowed = {"source_sha256", "evidence", "blocker", "edits", "additions", "steps",
               "resolve_warnings", "ingredient_edits", "slot_additions", "first_wort_hops",
               "source_yeast", "fermentation", "carbonation"}
    if set(review) - allowed or not review["evidence"].strip():
        raise ValueError("Unexpected or empty review")
    digest = entry["source_records"][0]["sha256"]
    if review["source_sha256"] != digest:
        raise ValueError("Reviewed recipe checksum changed")
    working = deepcopy(raw)
    source = working["VM"]["Recipe"]
    for edit in review.get("edits", []):
        edit_source(source, edit)

    # An explicit author correction takes precedence over generated loading sheets.
    recover_units(source)
    converted, warnings = convert_public(working)
    baseline, original_warnings = convert_public(raw)
    automatic = recovery(entry, working) or {}
    result = {"source_sha256": digest, "evidence": review["evidence"]}
    original_source = raw["VM"]["Recipe"]
    oversize = original_source["BatchSize"] >= 3.5 and any(
        error["Type"] == 10 and error["Description"] == "Batch size must be less than 3.5 Gallons."
        for error in original_source["Errors"])
    if automatic.get("batch_limit") or oversize:
        result["batch_limit"] = True

    resolved = set(original_warnings) - set(warnings)
    resolved.update(automatic.get("resolved", []))
    resolved.update(review.get("resolve_warnings", []))
    original = entry["recipe"]
    original_flags = original["provenance"]["warnings"]
    unknown = set(review.get("resolve_warnings", [])) - set(original_flags)
    if unknown:
        raise ValueError(f"Cannot resolve absent source warning: {unknown}")
    result["resolved"] = [w for w in original_flags if w in resolved]
    extra = [w for w in warnings if w not in original_warnings and w not in resolved]
    if extra:
        result["warnings"] = extra
    if review.get("blocker"):
        result["review"] = review["blocker"]

    ingredients = automatic.get("ingredients", converted["ingredients"])
    # Previous human reviews may have supplied quantities absent from the original.
    # Preserve those unless the new review explicitly amends or removes them.
    if previous:
        ingredients.extend(deepcopy(previous.get("additions", [])))
    for change in review.get("ingredient_edits", []):
        selector = {k: change[k] for k in ("name", "use", "minutes") if k in change}
        if not {"name", "use"} <= selector.keys():
            raise ValueError(f"Missing ingredient selector: {change}")
        ingredient = select_ingredient(ingredients, selector)
        if change.get("remove"):
            ingredients.remove(ingredient)
        else:
            ingredient.update(change["set"])
    ingredients.extend(deepcopy(review.get("additions", [])))
    for key, kind in (("slot_additions", "misc"), ("first_wort_hops", "hop")):
        for spec in review.get(key, []):
            selector = {"name": spec} if isinstance(spec, str) else spec
            ingredient = select_ingredient(ingredients, {**selector, "kind": kind, "use": "boil"})
            if ingredient["unit"] != "g" or ingredient["amount"] <= 0:
                raise ValueError(f"Slot ingredient needs a measured mass: {selector}")
            if kind == "hop":
                ingredient["use"] = "first-wort"
            else:
                ingredient["slot_addition"] = True
    if ingredients != original["ingredients"]:
        result["ingredients"] = ingredients
    for key in ("steps", "boil_minutes", "fermentation", "batch_liters", "efficiency"):
        value = review.get(key, converted[key])
        if value != original.get(key) and (key in review or value != baseline[key]):
            result[key] = value
    for key in ("source_yeast", "carbonation"):
        if key in review:
            result[key] = review[key]
    return result
