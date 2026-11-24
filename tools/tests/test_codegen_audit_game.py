"""codegen_audit_game reads shards `_full_NN.c` and 5-field dispatch rows."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import codegen_audit_game as audit  # noqa: E402

DISPATCH = """
typedef struct { uint32_t lo; uint32_t len; } PsxGameCodeRange;
static const PsxGameCodeRange r[] = {
    {0x80101000u, 0x8u},
};
    {0x801AB168u, 0x801AB168u, 1354u, 1u, func_801AB148},
    {0x801AB1B0u, 0x00000000u, 1355u, 1u, func_801AB1B0},
    case 0x80101008u: func_80101008(cpu); return 1;
"""


class AuditFormats(unittest.TestCase):
    def test_dispatch_rows_five_fields_and_case(self):
        got = audit.dispatch_table_addrs(DISPATCH)
        self.assertEqual([int(a, 16) for a in got], [0x801AB168, 0x801AB1B0, 0x80101008])

    def test_shards_and_single_file(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            (d / "X_full_00.c").write_text("a", encoding="utf-8")
            (d / "X_full_01.c").write_text("b", encoding="utf-8")
            text, parts = audit.read_full_sources(d / "X_full.c")
            self.assertEqual((text, len(parts)), ("a\nb", 2))
            (d / "X_full.c").write_text("z", encoding="utf-8")
            text, parts = audit.read_full_sources(d / "X_full.c")
            self.assertEqual(len(parts), 3)

    def test_missing(self):
        with tempfile.TemporaryDirectory() as t:
            self.assertEqual(audit.read_full_sources(Path(t) / "X_full.c")[1], [])


if __name__ == "__main__":
    unittest.main()
