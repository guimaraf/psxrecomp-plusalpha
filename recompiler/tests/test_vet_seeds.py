#!/usr/bin/env python3
"""Regression checks for aligned MIPS function boundaries in vet_seeds.py."""

from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from vet_seeds import vet  # noqa: E402


EXE_HEADER = 0x800
BASE_PHYS = 0x00010000
TEXT_SIZE = 0x100
FUNCTION_OFFSET = 0x40
FUNCTION_VADDR = 0x80000000 | BASE_PHYS | FUNCTION_OFFSET
JR_RA = 0x03E00008
NOP = 0x00000000
PROLOGUE = 0x27BDFFF0


def put32(data: bytearray, text_offset: int, word: int) -> None:
    struct.pack_into("<I", data, EXE_HEADER + text_offset, word)


def make_exe(padding_words: int, *, gap_word: int = NOP,
             delay_slot: int = NOP, prologue: bool = True) -> bytes:
    data = bytearray(EXE_HEADER + TEXT_SIZE)
    jr_offset = FUNCTION_OFFSET - 8 - padding_words * 4
    put32(data, jr_offset, JR_RA)
    put32(data, jr_offset + 4, delay_slot)
    for index in range(padding_words):
        put32(data, jr_offset + 8 + index * 4, gap_word)
    put32(data, FUNCTION_OFFSET, PROLOGUE if prologue else 0x8C880000)
    return bytes(data)


def check(expected: str, data: bytes, message: str) -> str:
    verdict, reason = vet(data, BASE_PHYS, TEXT_SIZE, FUNCTION_VADDR)
    if verdict != expected:
        raise AssertionError(
            f"{message}: expected {expected}, got {verdict}: {reason}"
        )
    return reason


def main() -> int:
    check("ACCEPT", make_exe(0), "immediate return boundary")

    reason = check(
        "WARN",
        make_exe(1, prologue=False),
        "one alignment NOP before frameless leaf",
    )
    if "1 alignment NOP" not in reason:
        raise AssertionError(f"alignment was not reported: {reason}")

    reason = check(
        "ACCEPT",
        make_exe(3, delay_slot=0x24840001),
        "three alignment NOPs and useful delay slot",
    )
    if "non-nop delay slot" not in reason or "3 alignment NOPs" not in reason:
        raise AssertionError(f"boundary details were not reported: {reason}")

    check(
        "REJECT",
        make_exe(1, gap_word=0x24020001),
        "non-NOP gap must not be treated as alignment",
    )
    check(
        "REJECT",
        make_exe(4),
        "more than three alignment NOPs must remain outside the heuristic",
    )

    print("PASS: vet_seeds aligned boundary regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
