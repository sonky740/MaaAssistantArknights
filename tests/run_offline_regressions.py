#!/usr/bin/env python3
"""Fail-closed entry point for the required offline regression cases."""
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

REQUIRED = {
    "tests.formation_probe_regression.FormationProbeTests": (
        "test_synthetic_both_buttons", "test_synthetic_mismatch", "test_synthetic_neither",
        "test_rejects_wrong_resolution_and_missing_file", "test_external_incident_mismatch"),
    "tests.formation_trace_summary_test.TraceTest": (
        "test_order_redaction_and_filter", "test_bad_json_and_unrelated_events"),
    "tests.test_offline_regressions.OCRRegressionTests": (
        "test_sarkaz_and_jiegarden", "test_sami"),
}
OPTIONAL = "tests.formation_probe_regression.FormationProbeTests.test_external_incident_mismatch"

def main():
    names = [f"{cls}.{method}" for cls, methods in REQUIRED.items() for method in methods]
    loader = unittest.TestLoader()
    suite = unittest.TestSuite(loader.loadTestsFromName(name) for name in names)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    allowed_skip = not os.environ.get("MAA_FORMATION_INCIDENT_IMAGE")
    unexpected_skips = [(test.id(), reason) for test, reason in result.skipped
                        if not (allowed_skip and test.id() == OPTIONAL and reason == "no external incident image")]
    if result.testsRun != len(names) or unexpected_skips or not result.wasSuccessful():
        print(f"Offline regression FAILED: expected {len(names)} tests, ran {result.testsRun}; unexpected skips: {unexpected_skips}", file=sys.stderr)
        return 1
    print(f"Offline regression PASSED: {result.testsRun - len(result.skipped)} cases passed; {len(result.skipped)} optional screenshot skip(s)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
