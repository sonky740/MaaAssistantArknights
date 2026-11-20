#!/usr/bin/env python3
"""Offline checks of the formation image probe, not ProcessTask/controller tests.

Requires Pillow and the OpenCV toolchain described by formation_state_probe.py.
No private screenshot is copied into the repository. To include an incident image,
set MAA_FORMATION_INCIDENT_IMAGE to its external path.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "tools/formation_state_probe.py"
TEMPLATES = ROOT / "resource/template/Roguelike/base"

class FormationProbeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="maa-formation-test-", dir=os.environ.get("TMPDIR"))
        self.addCleanup(self.temp.cleanup)
        self.image = Path(self.temp.name) / "screen.png"

    def screen(self, quick=False, confirm=False, size=(1280, 720)):
        im = Image.new("RGB", size, (23, 31, 47))
        if quick:
            with Image.open(TEMPLATES / "Roguelike@QuickFormation.png") as template:
                im.paste(template.convert("RGB"), (879, 19))
        if confirm:
            with Image.open(TEMPLATES / "Roguelike@FormationConfirm.png") as template:
                im.paste(template.convert("RGB"), (1040, 625))
        im.save(self.image)

    def probe(self, image=None, *options):
        return subprocess.run([sys.executable, str(PROBE), str(image or self.image), *options],
                              text=True, capture_output=True, cwd=ROOT)

    def test_synthetic_both_buttons(self):
        self.screen(quick=True, confirm=True)
        p = self.probe()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("QuickFormation present / FormationConfirm absent: False", p.stdout)
        self.assertIn("Roguelike@FormationConfirm: score=1", p.stdout)
        self.assertEqual(self.probe(None, "--expect-mismatch").returncode, 1)

    def test_synthetic_mismatch(self):
        self.screen(quick=True)
        p = self.probe(None, "--expect-mismatch")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("QuickFormation present / FormationConfirm absent: True", p.stdout)

    def test_synthetic_neither(self):
        self.screen()
        p = self.probe()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("QuickFormation present / FormationConfirm absent: False", p.stdout)
        self.assertEqual(self.probe(None, "--expect-mismatch").returncode, 1)

    def test_rejects_wrong_resolution_and_missing_file(self):
        self.screen(size=(640, 360))
        self.assertEqual(self.probe().returncode, 2)
        self.assertEqual(self.probe(Path(self.temp.name) / "missing.png").returncode, 2)

    @unittest.skipUnless(os.environ.get("MAA_FORMATION_INCIDENT_IMAGE"), "no external incident image")
    def test_external_incident_mismatch(self):
        p = self.probe(Path(os.environ["MAA_FORMATION_INCIDENT_IMAGE"]), "--expect-mismatch")
        self.assertEqual(p.returncode, 0, p.stderr + p.stdout)

if __name__ == "__main__":
    unittest.main(verbosity=2)
