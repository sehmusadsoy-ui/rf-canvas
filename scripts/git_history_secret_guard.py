#!/usr/bin/env python3
"""Read-only, no-egress Git object signature guard.

Prints finding *categories* only, never secret values, paths, or blob hashes.
Limited to known token/key signatures and blobs <= 2 MiB. This is not a
comprehensive history/credential audit, and false positives are possible.
"""
from __future__ import annotations

from collections import Counter
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MAX_BLOB_BYTES = 2 * 1024 * 1024
RULES = {
    "github-access-token": re.compile(rb"(?:gh[pousr]_[a-zA-Z0-9]{30,}|github_pat_[a-zA-Z0-9_]{60,})"),
    "aws-access-key-id": re.compile(rb"(?<![A-Z0-9])(?:AKIA|ASIA)[A-Z0-9]{16}(?![A-Z0-9])"),
    "private-key-block": re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----"),
    "slack-access-token": re.compile(rb"\bxox[baprs]-[a-zA-Z0-9-]{20,}\b"),
    "google-api-key": re.compile(rb"\bAIza[a-zA-Z0-9_-]{35}\b"),
    "stripe-secret-key": re.compile(rb"\bsk_live_[a-zA-Z0-9]{16,}\b"),
}


def git_bytes(*args: str) -> bytes:
    # No shell, no network, no untrusted code execution, no captured data in logs.
    return subprocess.run(
        ["git", *args], cwd=ROOT, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, check=True, timeout=120,
    ).stdout


def main() -> int:
    try:
        objects = git_bytes(
            "cat-file", "--batch-all-objects",
            "--batch-check=%(objectname) %(objecttype) %(objectsize)",
        )
    except (OSError, subprocess.SubprocessError):
        print("ERROR: cannot enumerate local Git objects; scan incomplete.")
        return 2

    if not objects.strip():
        print("ERROR: no Git objects found; scan incomplete.")
        return 2

    seen = Counter()
    scanned = 0
    skipped_large = 0
    for line in objects.splitlines():
        fields = line.split()
        if len(fields) != 3 or fields[1] != b"blob":
            continue
        try:
            size = int(fields[2])
        except ValueError:
            print("ERROR: invalid object metadata; scan incomplete.")
            return 2
        if size > MAX_BLOB_BYTES:
            skipped_large += 1
            continue
        sha = fields[0].decode("ascii")
        if not re.fullmatch(r"[a-fA-F0-9]{40,64}", sha):
            print("ERROR: unexpected object identifier; scan incomplete.")
            return 2
        try:
            data = git_bytes("cat-file", "blob", sha)
        except (OSError, subprocess.SubprocessError):
            print("ERROR: could not inspect a Git object; scan incomplete.")
            return 2
        scanned += 1
        for label, signature in RULES.items():
            if signature.search(data):
                seen[label] += 1

    print(f"Inspected {scanned} Git blobs; skipped {skipped_large} blobs > 2 MiB.")
    if seen:
        for label, count in sorted(seen.items()):
            print(f"REVIEW: {label} pattern found in {count} distinct Git blob(s).")
        print("Possible historical secret exposure: investigate privately and rotate live credentials.")
        return 1
    print("PASS: no matching known signatures in inspected Git blobs.")
    print("Important: this does not exclude other secrets or prove full history coverage.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
