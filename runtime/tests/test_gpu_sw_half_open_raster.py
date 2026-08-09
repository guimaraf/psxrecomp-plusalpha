#!/usr/bin/env python3
"""Regressao estrutural da cobertura half-open dos triangulos do GPU Software."""

from __future__ import annotations

from collections import Counter
from pathlib import Path
import re
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "src" / "gpu_sw_renderer.c"
RASTERIZERS = (
    "raster_flat_triangle",
    "raster_gouraud_triangle",
    "raster_textured_triangle",
    "raster_shaded_textured_triangle",
)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"static void {name}\s*\(", source)
    if match is None:
        raise AssertionError(f"funcao ausente: {name}")
    start = source.find("{", match.end())
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"corpo incompleto: {name}")


def triangle_coverage(vertices: tuple[tuple[int, int], ...]) -> set[tuple[int, int]]:
    """Replica apenas a escolha de pixels do scanline C, sem atributos."""
    (x0, y0), (x1, y1), (x2, y2) = sorted(vertices, key=lambda vertex: vertex[1])
    dy_total = y2 - y0
    if dy_total == 0:
        return set()

    pixels: set[tuple[int, int]] = set()
    for y in range(y0, y2):
        second_half = y >= y1
        seg_height = (y2 - y1) if second_half else (y1 - y0)
        if seg_height == 0:
            seg_height = 1
        alpha = (y - y0) / dy_total
        beta = ((y - y1) if second_half else (y - y0)) / seg_height
        xa = x0 + int((x2 - x0) * alpha)
        xb = (x1 + int((x2 - x1) * beta)) if second_half else (
            x0 + int((x1 - x0) * beta)
        )
        if xa > xb:
            xa, xb = xb, xa
        pixels.update((x, y) for x in range(xa, xb))
    return pixels


class HalfOpenRasterTest(unittest.TestCase):
    def test_all_rasterizers_use_half_open_bounds(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        for name in RASTERIZERS:
            with self.subTest(name=name):
                body = function_body(source, name)
                self.assertIn("for (int y = y0; y < y2; y++)", body)
                self.assertIn("int ex = min_i(xb, t->cx2 + 1);", body)
                self.assertIn("for (int x = sx; x < ex; x++)", body)
                self.assertNotIn("for (int y = y0; y <= y2; y++)", body)
                self.assertNotIn("for (int x = sx; x <= ex; x++)", body)

    def test_two_triangles_cover_quad_without_diagonal_overlap(self) -> None:
        first = triangle_coverage(((1, 1), (5, 1), (1, 5)))
        second = triangle_coverage(((1, 5), (5, 1), (5, 5)))
        counts = Counter(first)
        counts.update(second)

        expected = {(x, y) for y in range(1, 5) for x in range(1, 5)}
        self.assertEqual(expected, set(counts))
        self.assertTrue(all(count == 1 for count in counts.values()))


if __name__ == "__main__":
    unittest.main()
