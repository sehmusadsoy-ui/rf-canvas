#!/usr/bin/env python3
"""RF Canvas targeted source invariants (not an SDK build or security audit)."""
from pathlib import Path
import re
import sys

source = (Path(__file__).resolve().parents[1] / "rf_canvas.c").read_text(encoding="utf-8")
checks = {
    "allocation is checked before use": bool(re.search(
        r"malloc\s*\(\s*sizeof\(RfCanvasApp\)\s*\)\s*;\s*"
        r"(?://[^\n]*\n\s*)*if\s*\(\s*!app\s*\)\s*return\s+-1\s*;",
        source,
    )),
    "receive mode is initialized": "furi_hal_subghz_rx();" in source,
    "radio is idled and put to sleep on normal exit": (
        "furi_hal_subghz_idle();" in source and "furi_hal_subghz_sleep();" in source
    ),
    "no Sub-GHz transmit API": not re.search(r"\bfuri_hal_subghz_\w*tx\w*\s*\(", source, re.I),
    "no unbounded strcpy, strcat, sprintf, gets": not re.search(
        r"\b(?:strcpy|strcat|sprintf|gets)\s*\(", source
    ),
}
for name, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
if not all(checks.values()):
    print("Static guard failed. A real SDK build and device RF tests remain separate.")
    sys.exit(1)
print("Targeted source guard passed; no firmware build or device operation performed.")
