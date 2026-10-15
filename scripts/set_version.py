#!/usr/bin/env python3
"""
Update the application version in src/version.h, then commit it, create a
release/<version> branch and a v<version> tag at that commit.

Usage:
    python scripts/set_version.py <MAJOR.MINOR.PATCH> [--no-git] [--push [REMOTE]]

--push pushes the current branch, the release branch and the tag to REMOTE
(default: the only configured remote, or "origin").

Example:
    python scripts/set_version.py 1.2.0
    python scripts/set_version.py 1.2.0 --push
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent
VERSION_H = ROOT / "src" / "version.h"


def git(*args, check=True):
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                            text=True, encoding="utf-8")
    if check and result.returncode != 0:
        print(f"Error: git {' '.join(args)} failed:\n{result.stderr.strip()}")
        sys.exit(1)
    return result.stdout.strip()


def ref_exists(ref):
    return subprocess.run(["git", "rev-parse", "--verify", "--quiet", ref],
                          cwd=ROOT, capture_output=True).returncode == 0


def default_remote():
    remotes = git("remote").split()
    if len(remotes) == 1:
        return remotes[0]
    return "origin"


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("version", help="MAJOR.MINOR.PATCH (a leading 'v' is allowed)")
    parser.add_argument("--no-git", action="store_true",
                        help="only update src/version.h")
    parser.add_argument("--push", nargs="?", const="", metavar="REMOTE",
                        help="push current branch, release branch and tag")
    args = parser.parse_args()
    return args.version, not args.no_git, args.push


def main():
    raw_version, use_git, push_remote = parse_args()

    raw = raw_version.lstrip("v")
    parts = raw.split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        print(f"Error: version must be MAJOR.MINOR.PATCH (got '{raw_version}')")
        sys.exit(1)

    major, minor, patch = parts
    version_str = f"{major}.{minor}.{patch}"
    branch = f"release/{version_str}"
    tag = f"v{version_str}"

    # Validate git state before touching any file.
    if use_git:
        if ref_exists(f"refs/heads/{branch}"):
            print(f"Error: branch '{branch}' already exists")
            sys.exit(1)
        if ref_exists(f"refs/tags/{tag}"):
            print(f"Error: tag '{tag}' already exists")
            sys.exit(1)

    text = VERSION_H.read_text(encoding="utf-8")

    text = re.sub(r"(#define APP_VERSION_MAJOR\s+)\d+", rf"\g<1>{major}", text)
    text = re.sub(r"(#define APP_VERSION_MINOR\s+)\d+", rf"\g<1>{minor}", text)
    text = re.sub(r"(#define APP_VERSION_PATCH\s+)\d+", rf"\g<1>{patch}", text)
    text = re.sub(r'(#define APP_VERSION_STR\s+)"[\d.]+"', rf'\g<1>"{version_str}"', text)

    VERSION_H.write_text(text, encoding="utf-8")
    print(f"Version set to {version_str}  ({VERSION_H})")

    if not use_git:
        return

    # Commit only version.h, leaving any other staged changes untouched.
    version_rel = VERSION_H.relative_to(ROOT).as_posix()
    git("add", version_rel)
    if git("diff", "--cached", "--name-only", "--", version_rel):
        git("commit", "-m", f"Bump version to {tag}", "--", version_rel)
        print(f"Committed: Bump version to {tag}")
    else:
        print("version.h unchanged, tagging current HEAD")

    git("branch", branch)
    print(f"Created branch {branch}")
    git("tag", "-a", tag, "-m", f"Release {tag}")
    print(f"Created tag {tag}")

    if push_remote is not None:
        remote = push_remote or default_remote()
        current = git("rev-parse", "--abbrev-ref", "HEAD")
        refs = [branch, tag]
        if current != "HEAD":
            refs.insert(0, current)
        git("push", remote, *refs)
        print(f"Pushed {', '.join(refs)} to {remote}")
    else:
        print(f"Push with: git push <remote> {branch} {tag}")


if __name__ == "__main__":
    main()
