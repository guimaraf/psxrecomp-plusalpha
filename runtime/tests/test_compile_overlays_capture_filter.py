#!/usr/bin/env python3
"""Regression test for exact overlay capture micro-batch filtering.

Usage: python runtime/tests/test_compile_overlays_capture_filter.py
Exit 0 = PASS.
"""

from pathlib import Path
import argparse
import base64
import binascii
import importlib.util
import sys


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "compile_overlays.py"
SPEC = importlib.util.spec_from_file_location("compile_overlays", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def make_capture(load_addr: int, data: bytes) -> dict:
    return {
        "load_addr": f"0x{load_addr:08X}",
        "size": len(data),
        "bytes_b64": base64.b64encode(data).decode("ascii"),
    }


def main() -> int:
    stage10 = make_capture(0x801AE000, b"stage10-code")
    same_region_other_variant = make_capture(0x801AE000, b"other-code")
    main_text = make_capture(0x80010000, b"main-text")
    captures = [main_text, stage10, same_region_other_variant]

    stage10_crc = binascii.crc32(b"stage10-code") & 0xFFFFFFFF
    key = MODULE.parse_capture_key(f"0x801AE000:0x{stage10_crc:08X}")
    assert key == (0x001AE000, stage10_crc), "virtual region was not canonicalized"

    selected, missing = MODULE.select_captures_by_key(captures, [key])
    assert selected == [stage10], "filter selected the wrong same-address variant"
    assert not missing, "present capture key was reported missing"

    absent = (0x001AE000, 0xDEADBEEF)
    selected, missing = MODULE.select_captures_by_key(captures, [absent])
    assert selected == [], "absent key selected a capture"
    assert missing == {absent}, "absent key did not fail closed"

    selected, missing = MODULE.select_captures_by_key(captures, [])
    assert selected is captures and not missing, "default path must keep all captures"

    for invalid in ("0x001AE000", "bad:0x1", "0x1:0x100000000"):
        try:
            MODULE.parse_capture_key(invalid)
        except argparse.ArgumentTypeError:
            pass
        else:
            raise AssertionError(f"invalid capture key accepted: {invalid}")

    print("PASS: exact capture-key filtering is canonical, variant-safe, and fail-closed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, KeyError, ValueError) as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
