#!/usr/bin/env python3
"""Apply explicitly reviewed catalogue exclusions while preserving source files.

The review names exact entries and original-record checksums. This module never
guesses that a recipe is disposable from a substring or an ingredient count.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import zipfile


POLICY_PATH = Path(__file__).with_name("catalog-curation.json")


def load_policy(path: Path = POLICY_PATH):
    policy = json.loads(path.read_text())
    if policy.get("schema_version") != 1:
        raise ValueError("unsupported catalogue curation schema")
    if not isinstance(policy.get("removed_entries"), list):
        raise ValueError("curation must list reviewed entries")
    return policy


def apply_reviewed_exclusions(entries, sources, excluded, policy):
    """Return new entries/source counts/exclusions; validate the whole review first."""
    counts = {source["id"]: source for source in sources}
    for source_id, commit in policy["source_commits"].items():
        if source_id not in counts or counts[source_id]["commit"] != commit:
            raise ValueError(f"curation source commit changed: {source_id}")

    by_id = {entry["id"]: entry for entry in entries}
    if len(by_id) != len(entries):
        raise ValueError("duplicate catalogue entry ID")
    reviewed = {}
    for decision in policy["removed_entries"]:
        entry_id = decision["id"]
        if entry_id in reviewed:
            raise ValueError(f"duplicate curation decision: {entry_id}")
        if not decision.get("reason") or not decision.get("evidence"):
            raise ValueError(f"curation needs a reason and evidence: {entry_id}")
        entry = by_id.get(entry_id)
        if entry is None:
            raise ValueError(f"reviewed entry is missing: {entry_id}")
        if entry["source_id"] != decision["source_id"] or entry["name"] != decision["name"]:
            raise ValueError(f"reviewed entry identity changed: {entry_id}")
        if entry.get("featured"):
            raise ValueError(f"curation cannot remove a preserved starter program: {entry_id}")
        key = lambda record: (record["path"], record["id"])
        if sorted(entry["source_records"], key=key) != sorted(decision["source_records"], key=key):
            raise ValueError(f"reviewed original records changed: {entry_id}")
        reviewed[entry_id] = decision

    new_sources = copy.deepcopy(sources)
    new_counts = {source["id"]: source for source in new_sources}
    new_excluded = copy.deepcopy(excluded)
    seen_records = {(item["source_id"], item["path"]) for item in new_excluded}
    retained = []
    for entry in entries:
        decision = reviewed.get(entry["id"])
        if decision is None:
            retained.append(entry)
            continue
        source = new_counts[entry["source_id"]]
        category = "curated_" + decision["reason"]
        for record in entry["source_records"]:
            identity = (entry["source_id"], record["path"])
            if identity in seen_records:
                raise ValueError(f"original record excluded twice: {identity}")
            seen_records.add(identity)
            new_excluded.append({
                "source_id": entry["source_id"], "reason": category,
                "catalog_id": entry["id"], "catalog_name": entry["name"],
                "review_evidence": decision["evidence"], **record,
            })
        count = len(entry["source_records"])
        source["valid_count"] -= count
        source["excluded_count"] += count
        source["excluded_reasons"][category] = source["excluded_reasons"].get(category, 0) + count
        if source["valid_count"] < 0:
            raise ValueError(f"invalid retained-record count: {source['id']}")

    for source in new_sources:
        source["entry_count"] = sum(entry["source_id"] == source["id"] for entry in retained)
    return retained, new_sources, new_excluded


def curate_bundle(source: Path, destination: Path, policy_path: Path = POLICY_PATH):
    """Reproduce the curation from the exact original bundle, without fetching data."""
    from build_catalog import dump, write_zip

    policy = load_policy(policy_path)
    if hashlib.sha256(source.read_bytes()).hexdigest() != policy["baseline_bundle_sha256"]:
        raise ValueError("input is not the exact reviewed original catalogue bundle")
    with zipfile.ZipFile(source) as archive:
        files = {name: archive.read(name) for name in archive.namelist()}
    if "curation.json" in files:
        raise ValueError("input bundle has already been curated")
    index = json.loads(files["index.json"])
    entries = [json.loads(files[f"entries/{summary['id']}.json"]) for summary in index]
    manifest = json.loads(files["manifest.json"])
    entries, sources, excluded = apply_reviewed_exclusions(
        entries, manifest["sources"], json.loads(files["excluded.json"]), policy,
    )
    retained_ids = {entry["id"] for entry in entries}
    for name in list(files):
        if name.startswith("entries/") and name.removeprefix("entries/").removesuffix(".json") not in retained_ids:
            del files[name]
    files["index.json"] = dump([summary for summary in index if summary["id"] in retained_ids])
    manifest["sources"] = sources
    manifest["entry_count"] = len(entries)
    files["manifest.json"] = dump(manifest)
    files["excluded.json"] = dump(sorted(excluded, key=lambda item: (item["source_id"], item["path"])))
    files["curation.json"] = dump(policy)
    write_zip(destination, files)
    return {"entries": len(entries), "reviewed_removals": len(policy["removed_entries"]),
            "bytes": destination.stat().st_size,
            "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Exact uncurated bundle recorded by the review")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--policy", type=Path, default=POLICY_PATH)
    args = parser.parse_args()
    print(json.dumps(curate_bundle(args.input, args.output, args.policy), indent=2))


if __name__ == "__main__":
    main()
