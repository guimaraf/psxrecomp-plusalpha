"""Resolve nested title projects without copying a partial SDK over their data."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "tools")]
import psxrecomp_cli as cli


class FrameworkLayoutTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.title = self.root / "PlusAlphaProject"
        self.title.mkdir()
        self.framework = self.root / "psxrecomp"
        (self.framework / "bios").mkdir(parents=True)
        (self.framework / "bios" / "OpenBIOS.toml").write_text("[program]\n")
        (self.framework / "recompiler").mkdir()
        (self.framework / "recompiler" / "CMakeLists.txt").write_text("# source checkout\n")

    def test_generation_uses_the_sibling_source_checkout(self):
        with mock.patch.object(cli, "ROOT", self.root / "another-sdk"):
            self.assertEqual(cli.framework_root(self.title), self.framework)

    def test_ensure_does_not_seed_a_second_framework_in_the_title(self):
        with mock.patch.object(cli, "_copy_missing", side_effect=AssertionError("must not seed")):
            self.assertEqual(cli.ensure_framework(self.title), self.framework)
        self.assertFalse((self.title / "psxrecomp").exists())

    def test_conventional_game_owned_framework_has_priority(self):
        nested = self.title / "psxrecomp"
        (nested / "bios").mkdir(parents=True)
        (nested / "bios" / "OpenBIOS.toml").write_text("[program]\n")
        self.assertEqual(cli.framework_root(self.title), nested)
        self.assertEqual(cli.ensure_framework(self.title), nested)


if __name__ == "__main__":
    unittest.main()
