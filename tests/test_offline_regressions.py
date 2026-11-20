"""Discoverable adapters for the offline diagnostics."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

from tests.formation_probe_regression import FormationProbeTests
from tests.formation_trace_summary_test import TraceTest

ROOT = Path(__file__).resolve().parents[1]

class OCRRegressionTests(unittest.TestCase):
    def run_regression(self, script):
        env = os.environ.copy()
        env.pop("PYTHONOPTIMIZE", None)
        result = subprocess.run([sys.executable, str(ROOT / "tests" / script)],
                                cwd=ROOT, env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sarkaz_and_jiegarden(self):
        self.run_regression("kr_encounter_ocr_regression.py")

    def test_sami(self):
        self.run_regression("sami_kr_encounter_ocr_regression.py")
