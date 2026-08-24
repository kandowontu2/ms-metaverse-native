#!/usr/bin/env python3
"""Reject original cursor/icon resources in a public-release executable."""

from __future__ import annotations

import argparse
from pathlib import Path

import pefile


ORIGINAL_ART_RESOURCE_TYPES = {
    1: "cursor",
    3: "icon",
    12: "group cursor",
    14: "group icon",
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()

    pe = pefile.PE(str(args.executable), fast_load=False)
    resources = getattr(pe, "DIRECTORY_ENTRY_RESOURCE", None)
    resource_types: set[int] = set()
    if resources is not None:
        resource_types = {
            entry.id for entry in resources.entries if entry.id is not None
        }
    forbidden = resource_types.intersection(ORIGINAL_ART_RESOURCE_TYPES)
    if forbidden:
        labels = ", ".join(
            ORIGINAL_ART_RESOURCE_TYPES[resource_type]
            for resource_type in sorted(forbidden)
        )
        raise SystemExit(
            f"public executable contains optional original-art resources: {labels}"
        )
    if 16 not in resource_types:
        raise SystemExit("public executable is missing version metadata")
    print(
        "Public PE resource audit passed: "
        + ", ".join(str(value) for value in sorted(resource_types))
    )


if __name__ == "__main__":
    main()
