"""SVG/XAML compatibility regressions for the shipped icon assets."""
import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("icon_packs", ROOT / "tools/gen_icon_packs.py")
ICONS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ICONS)


class PathCompatibility(unittest.TestCase):
    def test_closed_subpaths_restore_svg_origin_for_relative_moves(self):
        self.assertEqual(
            ICONS.normalize("M3 3.5v13zm14 .75v11.5zm-1.39-.2"),
            "M 3 3.5 V 16.5 Z M 17 4.25 V 15.75 Z M 15.61 4.05",
        )

    def test_real_fluent_media_shapes_keep_their_inner_contours(self):
        path = ROOT / "design/icons/packs/fluent/previous.svg"
        data = ET.parse(path).getroot()[0].get("d")
        normalized = ICONS.normalize(data)
        self.assertIn("Z M 17 4.25", normalized)
        self.assertIn("Z M 15.61 4.05", normalized)
        path = ROOT / "design/icons/packs/fluent/play.svg"
        normalized = ICONS.normalize(ET.parse(path).getroot()[0].get("d"))
        self.assertIn("Z M 16.74 10.44", normalized)

    def test_relative_curve_controls_share_the_segment_origin(self):
        self.assertEqual(
            ICONS.normalize("m2 3c1 2 3 4 5 6s7 8 9 10q1 2 3 4t5 6"),
            "M 2 3 C 3 5 5 7 7 9 S 14 17 16 19 Q 17 21 19 23 T 24 29",
        )

    def test_compact_arc_flags_and_implicit_move_lines(self):
        self.assertEqual(
            ICONS.normalize("m1 2 3 4a5 6 0 0110-2"),
            "M 1 2 L 4 6 A 5 6 0 0 1 14 4",
        )

    def test_every_pack_icon_compiles_to_absolute_commands(self):
        for path in (ROOT / "design/icons/packs").glob("*/*.svg"):
            with self.subTest(path=path):
                data, width, height, _ = ICONS.compile_svg(path)
                self.assertGreater(width, 0)
                self.assertGreater(height, 0)
                self.assertNotRegex(data, r"[mlhvcsqtaez]")


if __name__ == "__main__":
    unittest.main()
