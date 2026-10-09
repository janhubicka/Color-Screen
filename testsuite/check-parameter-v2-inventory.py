#!/usr/bin/env python3
"""Guard the native JSON v2 migration field inventory against CSP writer drift.

The inventory is not evidence that a field has been migrated: pending-native
remains an explicit block on enabling the full JSON v2 writer. This test
ensures that an added/removed legacy keyword cannot silently escape the audit.
Nested mesh/MTF/correction serializers and Qt metadata require separate
manual review and are called out in the v2 migration plan.
"""

from __future__ import annotations

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAVE_SOURCE = ROOT / "src/libcolorscreen/loadsave.C"
INVENTORY = ROOT / "testsuite/parameter-v2-key-inventory.tsv"
STATUSES = {"native-component", "pending-native", "excluded-runtime"}
ADDITIONAL = {
    "profile_spots",
    "ignore_infrared",
    "demosaiced_scaling",
    "observer_whitepoint",
    "final_angle",
    "final_ratio",
    "image_area",
    "output_profile",
    "output_gamma",
    "gamut_warning",
}


def legacy_writer_keys(source: str) -> set[str]:
    """Extract explicitly printed keyword prefixes from SAVE_CSP's body."""
    start = source.find("\nsave_csp (")
    if start < 0:
        raise ValueError("save_csp definition not found")
    end = source.find("\nstatic bool\nskipwhitespace", start)
    if end < 0:
        raise ValueError("save_csp end not found")
    keys = set()
    for line in source[start:end].splitlines():
        if line.lstrip().startswith("//"):
            continue
        keys.update(re.findall(r'"([A-Za-z][A-Za-z0-9_-]*):', line))
    return keys


def main() -> int:
    source = SAVE_SOURCE.read_text(encoding="utf-8")
    actual = legacy_writer_keys(source)
    records = {}
    with INVENTORY.open(encoding="utf-8", newline="") as handle:
        lines = (line for line in handle if line.strip() and not line.startswith("#"))
        for row in csv.DictReader(lines, delimiter="\t"):
            if set(row) != {"source", "key", "v2_section", "status"}:
                raise ValueError(f"unexpected v2 inventory columns: {sorted(row)}")
            key = (row["source"], row["key"])
            if key in records:
                raise ValueError(f"duplicate v2 inventory entry: {key}")
            if row["status"] not in STATUSES:
                raise ValueError(f"invalid v2 coverage state for {key}")
            if not row["v2_section"].strip():
                raise ValueError(f"missing future v2 ownership for {key}")
            records[key] = row
    tracked = {key for source_kind, key in records if source_kind == "legacy-csp"}
    extra = {key for source_kind, key in records if source_kind == "other-v1-or-qt"}
    unsupported = {kind for kind, _ in records} - {"legacy-csp", "other-v1-or-qt"}
    differences = {
        "untracked legacy saved keywords": sorted(actual - tracked),
        "stale legacy entries": sorted(tracked - actual),
        "missing structured/Qt supplemental fields": sorted(ADDITIONAL - extra),
        "unexpected supplemental fields": sorted(extra - ADDITIONAL),
        "unknown inventory source kinds": sorted(unsupported),
    }
    broken = {name: values for name, values in differences.items() if values}
    if broken:
        for label, values in broken.items():
            print(f"{label}: {', '.join(values)}", file=sys.stderr)
        return 1

    print(
        f"v2 migration coverage inventory: {len(tracked)} legacy keys and "
        f"{len(extra)} supplemental keys accounted for"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
