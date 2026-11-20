#!/usr/bin/env python3
"""Replay observed Sami title through the ordered Boost.Regex OCR rules."""
import os
from pathlib import Path
import subprocess
import tempfile
from kr_encounter_ocr_regression import ROOT, BOOST, CPP, load

rules = load("resource/global/YoStarKR/resource/tasks/Roguelike/base.json")["Roguelike@StageEncounterOcr"]["ocrReplace"]
classes = load("resource/global/YoStarKR/resource/ocr_config.json")["equivalence_classes"]
expanded = []
for pattern, replacement in rules:
    for group in classes:
        if len(group) > 1:
            for element in group:
                pattern = pattern.replace(element, "(?:" + "|".join(group) + ")")
    expanded.append((pattern, replacement))
cases = [("산미의 언어", "萨米之语"), ("사미의 언어", "萨米之语"),
         ("산미의 언어추가", None), ("앞산미의 언어", None),
         ("산미의 어깨를 지나", None), ("없는이름", None)]
payload = str(len(expanded)) + "\n" + "".join(p + "\n" + r + "\n" for p, r in expanded)
payload += "".join(raw + "\n" for raw, _ in cases)
with tempfile.TemporaryDirectory(prefix="maa-sami-boost-", dir=os.environ.get("TMPDIR")) as tmp:
    cpp, exe = Path(tmp) / "test.cpp", Path(tmp) / "test"
    cpp.write_text(CPP, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-I", str(BOOST), str(cpp), "-o", str(exe)], check=True)
    outputs = subprocess.run([str(exe)], input=payload, text=True, capture_output=True, check=True).stdout.splitlines()
if len(outputs) != len(cases):
    raise RuntimeError(f"expected {len(cases)} OCR outputs, got {len(outputs)}")
names = list(dict.fromkeys(x["name"] for x in load("resource/roguelike/Sami/encounter/default.json")["stage"]))
errors = 0
for (raw, expected), output in zip(cases, outputs):
    match = next((name for name in names if name in output), None)
    ok = match == expected
    print(f"{'PASS' if ok else 'FAIL'} {raw!r}: replaced={output!r} match={match!r}")
    errors += not ok
for theme in ("Sarkaz", "JieGarden", "Mizuki"):
    required = [x["name"] for x in load(f"resource/roguelike/{theme}/encounter/default.json")["stage"]]
    for output in outputs[:2]:
        match = next((name for name in required if name in output), None)
        print(f"{'PASS' if match is None else 'FAIL'} {theme}: replaced={output!r} match={match!r}")
        errors += match is not None
if errors:
    raise SystemExit(f"{errors} failure(s)")
print("Sami KR OCR regression passed")
