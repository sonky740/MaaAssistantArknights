#!/usr/bin/env python3
"""Exercise the KR encounter replacement pipeline with Boost.Regex.

Run from any directory: python3 tests/kr_encounter_ocr_regression.py
Requires a C++17 compiler and Boost headers (BOOST_INCLUDE_DIR or vcpkg).
No game or installed resources are touched.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BOOST = Path(os.environ.get("BOOST_INCLUDE_DIR", ROOT / "src/MaaUtils/MaaDeps/vcpkg/installed/maa-arm64-osx/include"))
CPP = r"""
#include <boost/regex.hpp>
#include <codecvt>
#include <iostream>
#include <locale>
#include <string>
#include <vector>
int main() {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> cv;
    size_t n; std::cin >> n; std::cin.ignore();
    std::vector<std::pair<std::wstring, std::wstring>> rules;
    for (size_t i = 0; i < n; ++i) {
        std::string p, r; std::getline(std::cin, p); std::getline(std::cin, r);
        rules.emplace_back(cv.from_bytes(p), cv.from_bytes(r));
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        auto text = cv.from_bytes(line);
        for (const auto& [p, r] : rules)
            text = boost::regex_replace(text, boost::wregex(p), r,
                                        boost::regex_constants::match_not_dot_newline);
        std::cout << cv.to_bytes(text) << '\n';
    }
}
"""

def load(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))

def main():
    task_file = os.environ.get("KR_TASKS_FILE", "resource/global/YoStarKR/resource/tasks/Roguelike/base.json")
    rules = load(task_file)["Roguelike@StageEncounterOcr"]["ocrReplace"]
    # Mirror OCRerConfig::set_replace: expand each configured equivalence
    # class inside each regex before OCRer performs ordered replacements.
    classes = load("resource/global/YoStarKR/resource/ocr_config.json")["equivalence_classes"]
    expanded = []
    for pattern, replacement in rules:
        for eq_class in classes:
            if len(eq_class) > 1:
                group = "(?:" + "|".join(eq_class) + ")"
                for element in eq_class:
                    pattern = pattern.replace(element, group)
        expanded.append((pattern, replacement))
    rules = expanded
    base = load("resource/tasks/Roguelike/base.json")["Roguelike@StageEncounterOcr"]
    if base.get("fullMatch", False) or base.get("replaceFull", False):
        raise RuntimeError("unsupported OCR matching configuration")
    tests = {"만남»:observed": ("만남»", "相遇"), "만남H:observed": ("만남H", "相遇"), "만남:exact": ("만남", "相遇"),
             "의혹해소:other": ("의혹해소", "解惑"),
             "꿈속의만남:other": ("꿈속의만남", "梦中之会"),
             "만남의기억:negative": ("만남의기억", None),
             "없는이름:negative": ("없는이름", None),
             "만남»추가:negative": ("만남»추가", None),
             "만남H추가:negative": ("만남H추가", None),
             "앞만남H:negative": ("앞만남H", None)}
    payload = str(len(rules)) + "\n" + "".join(p + "\n" + r + "\n" for p, r in rules)
    payload += "".join(raw + "\n" for raw, _ in tests.values())
    with tempfile.TemporaryDirectory(prefix="maa-boost-ocr-", dir=os.environ.get("TMPDIR")) as tmp:
        cpp = Path(tmp) / "test.cpp"
        exe = Path(tmp) / "test"
        cpp.write_text(CPP, encoding="utf-8")
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-I", str(BOOST), str(cpp), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], input=payload, text=True, capture_output=True, check=True)
    outputs = result.stdout.splitlines()
    if len(outputs) != len(tests):
        raise RuntimeError(f"expected {len(tests)} OCR outputs, got {len(outputs)}")
    failed = 0
    for theme in ("Sarkaz", "JieGarden"):
        stages = load(f"resource/roguelike/{theme}/encounter/default.json")["stage"]
        required = list(dict.fromkeys(event["name"] for event in stages))
        if "相遇" not in required:
            raise RuntimeError(f"{theme}: required encounter 相遇 is missing")
        for (label, (_, expected)), out in zip(tests.items(), outputs):
            # OCRer uses the first required name contained in normalized text.
            # Relevant KR equivalence class is spaces; these cases contain none.
            match = next((name for name in required if name in out), None)
            ok = (match == expected) if expected in (None, "相遇", "解惑") else (out == expected)
            print(f"{'PASS' if ok else 'FAIL'} {theme} {label}: replaced={out!r}, required={match!r}")
            failed += not ok
    for theme in ("Sami", "Mizuki"):
        required = [event["name"] for event in load(f"resource/roguelike/{theme}/encounter/default.json")["stage"]]
        out = outputs[list(tests).index("만남H:observed")]
        match = next((name for name in required if name in out), None)
        ok = match is None
        print(f"{'PASS' if ok else 'FAIL'} {theme} 만남H:cross-theme required={match!r}")
        failed += not ok
    if failed:
        raise SystemExit(f"{failed} failure(s)")
    print("All KR encounter Boost.Regex regression cases passed")

if __name__ == "__main__":
    main()
