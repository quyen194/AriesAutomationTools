#!/usr/bin/env python3
"""
Update the application version in src/version.h.

Usage:
    python scripts/set_version.py <MAJOR.MINOR.PATCH>

Example:
    python scripts/set_version.py 1.2.0
"""

import re
import sys
from pathlib import Path

VERSION_H = Path(__file__).parent.parent / "src" / "version.h"


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    raw = sys.argv[1].lstrip("v")
    parts = raw.split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        print(f"Error: version must be MAJOR.MINOR.PATCH (got '{sys.argv[1]}')")
        sys.exit(1)

    major, minor, patch = parts
    version_str = f"{major}.{minor}.{patch}"

    text = VERSION_H.read_text(encoding="utf-8")

    text = re.sub(r"(#define APP_VERSION_MAJOR\s+)\d+", rf"\g<1>{major}", text)
    text = re.sub(r"(#define APP_VERSION_MINOR\s+)\d+", rf"\g<1>{minor}", text)
    text = re.sub(r"(#define APP_VERSION_PATCH\s+)\d+", rf"\g<1>{patch}", text)
    text = re.sub(r'(#define APP_VERSION_STR\s+)"[\d.]+"', rf'\g<1>"{version_str}"', text)

    VERSION_H.write_text(text, encoding="utf-8")
    print(f"Version set to {version_str}  ({VERSION_H})")


if __name__ == "__main__":
    main()
