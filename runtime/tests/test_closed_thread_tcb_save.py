#!/usr/bin/env python3
"""Regression test for ChangeTh after CloseTh.

The real BIOS SYS(03) saves the outgoing context even if CloseTh has already
marked that TCB free. OpenTh later reuses the slot without initializing SR, so
skipping the save can resurrect an interrupt-disabled SR and deadlock CD/VBlank
delivery in the reopened thread.

Usage: python runtime/tests/test_closed_thread_tcb_save.py
Exit 0 = PASS.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
TRAPS = ROOT / "runtime" / "src" / "traps.c"


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"\bstatic\s+int\s+{re.escape(name)}\s*\([^;]*?\)\s*\{{",
        source,
        re.S,
    )
    if not match:
        raise AssertionError(f"missing function definition: {name}")
    start = match.end()
    depth = 1
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos]
    raise AssertionError(f"unterminated function definition: {name}")


def require_order(body: str, first: str, second: str, message: str) -> None:
    first_at = body.find(first)
    second_at = body.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(message)


def main() -> int:
    source = TRAPS.read_text(encoding="utf-8")
    save = "psx_save_context_to_tcb(cpu, current_tcb, cpu->gpr[31]);"

    hle = function_body(source, "psx_request_thread_switch")
    if hle.count(save) != 1:
        raise AssertionError("HLE ChangeTh must save the outgoing TCB exactly once")
    require_order(
        hle,
        save,
        "psx_set_current_tcb(cpu, target_tcb);",
        "HLE ChangeTh selects the target before saving the outgoing TCB",
    )
    if re.search(r"if\s*\([^)]*current_tcb[^)]*0x4000u[^)]*\)\s*\{\s*" + re.escape(save), hle, re.S):
        raise AssertionError("HLE ChangeTh still skips the save for a closed TCB")

    legacy = function_body(source, "psx_change_thread_fiber")
    if legacy.count(save) != 1:
        raise AssertionError("legacy ChangeTh must save the outgoing TCB exactly once")
    require_order(
        legacy,
        save,
        "if (current_state == 0x4000u)",
        "legacy ChangeTh still gates the TCB save on the runnable state",
    )

    print("PASS: ChangeTh saves closed outgoing TCB context before switching")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
