#!/usr/bin/env python3
"""Offline formation-state probe. Never writes or embeds the supplied screenshot.

Run from any directory:
  python3 tools/formation_state_probe.py /path/to/1280x720.png --expect-mismatch
Uses the repo's macOS MaaDeps OpenCV; set MAA_OPENCV_PREFIX for another installation.
This is an OpenCV reproduction, not a ProcessTask/controller or callback test.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r"""
#include <opencv2/opencv.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        if (argc != 9) throw std::runtime_error("invalid arguments");
        cv::Mat image = cv::imread(argv[1]), templ = cv::imread(argv[2]);
        if (image.empty() || templ.empty()) throw std::runtime_error("cannot read image or template");
        if (image.cols != 1280 || image.rows != 720) throw std::runtime_error("expected normalized 1280x720 screenshot");
        cv::Rect roi(std::stoi(argv[3]), std::stoi(argv[4]), std::stoi(argv[5]), std::stoi(argv[6]));
        if (roi.x < 0 || roi.y < 0 || roi.width <= 0 || roi.height <= 0 ||
            roi.x + roi.width > image.cols || roi.y + roi.height > image.rows ||
            templ.cols > roi.width || templ.rows > roi.height) throw std::runtime_error("invalid ROI or template size");
        cv::Mat src_rgb, templ_rgb, result, valid;
        cv::cvtColor(image(roi), src_rgb, cv::COLOR_BGR2RGB);
        cv::cvtColor(templ, templ_rgb, cv::COLOR_BGR2RGB);
        if (std::string(argv[7]) == "none") cv::matchTemplate(src_rgb, templ_rgb, result, cv::TM_CCOEFF_NORMED);
        else throw std::runtime_error("masked matching is unsupported");
        cv::inRange(result, 0.0f, 1.0f + 1e-5f, valid);
        // Matcher::analyze also calls minMaxLoc with this mask when it is empty;
        // OpenCV returns score zero, which is below these positive thresholds.
        double score; cv::Point pos;
        cv::minMaxLoc(result, nullptr, &score, nullptr, &pos, valid);
        double threshold = std::stod(argv[8]);
        std::cout << score << " " << threshold << " " << (score >= threshold) << " "
                  << pos.x + roi.x << "," << pos.y + roi.y << "," << templ.cols << "," << templ.rows << "\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 2; }
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "image", type=Path, help="external, normalized 1280x720 screenshot"
    )
    parser.add_argument(
        "--expect-mismatch",
        action="store_true",
        help="fail unless QuickFormation matches but FormationConfirm does not",
    )
    parser.add_argument(
        "--resource-root",
        type=Path,
        default=ROOT / "resource",
        help="resource tree to inspect (default: repository resource)",
    )
    args = parser.parse_args()
    if not args.image.is_file():
        raise RuntimeError(f"image not found: {args.image}")
    resource = args.resource_root
    base_path = resource / "tasks/Roguelike/base.json"
    tasks = json.loads(base_path.read_text())
    types = ROOT / "src/MaaCore/Common/AsstTypes.h"
    match = re.search(r"TemplThresholdDefault\s*=\s*([0-9.]+)", types.read_text())
    if not match:
        raise RuntimeError("cannot find project default template threshold")
    default_threshold = float(match.group(1))
    names = ["Roguelike@QuickFormation", "Roguelike@FormationConfirm"]
    # TaskData implicitly derives an @-qualified task from any existing suffix.
    # Reject competing definitions and suffix bases across the entire supplied tree.
    suffixes = {name[i + 1 :] for name in names for i, c in enumerate(name) if c == "@"}
    for path in resource.rglob("*.json"):
        try:
            obj = json.loads(path.read_text())
        except (ValueError, UnicodeError) as exc:
            raise RuntimeError(f"cannot inspect resource JSON {path}: {exc}") from exc
        if not isinstance(obj, dict):
            continue
        for key in obj:
            if (
                key in suffixes
                or (key in names and path != base_path)
                or any(key.endswith("@" + name) for name in names)
            ):
                raise RuntimeError(f"ambiguous task definition {key} in {path}")
    if tasks[names[0]].get("next") != [names[1]]:
        raise RuntimeError("QuickFormation next changed; review this probe")
    prefix = Path(
        os.environ.get(
            "MAA_OPENCV_PREFIX",
            ROOT / "src/MaaUtils/MaaDeps/vcpkg/installed/maa-arm64-osx",
        )
    )
    lib = prefix / "lib"
    with tempfile.TemporaryDirectory(
        prefix="maa-formation-", dir=os.environ.get("TMPDIR")
    ) as tmp:
        src, exe = Path(tmp) / "probe.cpp", Path(tmp) / "probe"
        src.write_text(SOURCE)
        subprocess.run(
            [
                "c++",
                "-std=c++17",
                "-O2",
                str(src),
                "-I" + str(prefix / "include/opencv4"),
                "-L" + str(lib),
                "-lopencv_world4",
                "-Wl,-rpath," + str(lib),
                "-o",
                str(exe),
            ],
            check=True,
        )
        results = {}
        for name in names:
            task = tasks[name]
            if "baseTask" in task:
                raise RuntimeError(
                    f"{name}: inheritance/override changed; review this probe"
                )
            if (
                task.get("algorithm", "MatchTemplate") != "MatchTemplate"
                or task.get("method", "Ccoeff") != "Ccoeff"
            ):
                raise RuntimeError(f"{name}: unsupported matching algorithm")
            threshold = task.get("templThreshold", default_threshold)
            if not isinstance(threshold, (int, float)):
                raise RuntimeError(f"{name}: unsupported threshold")
            mask = task.get("maskRange")
            if (
                mask is not None
                or task.get("maskSrc", False)
                or task.get("maskClose", False)
            ):
                raise RuntimeError(f"{name}: unsupported mask configuration")
            templ_name = task.get("template", name + ".png")
            if not isinstance(templ_name, str):
                raise RuntimeError(f"{name}: unsupported multiple templates")
            matches = list((resource / "template").rglob(templ_name))
            if len(matches) != 1:
                raise RuntimeError(
                    f"{name}: expected one template, found {len(matches)}"
                )
            roi = task["roi"]
            output = subprocess.check_output(
                [
                    str(exe),
                    str(args.image),
                    str(matches[0]),
                    *map(str, roi),
                    "none",
                    str(threshold),
                ],
                text=True,
            )
            score, used_threshold, passed, rect = output.strip().split()
            results[name] = passed == "1"
            print(
                f"{name}: score={score} threshold={used_threshold} match={bool(int(passed))} rect={rect} roi={roi}"
            )
    mismatch = results[names[0]] and not results[names[1]]
    print(f"QuickFormation present / FormationConfirm absent: {mismatch}")
    print("RoguelikeFormationOper: unsupported masked project matcher; not measured.")
    print(
        "Diagnostic only: supplied resource tree inspected; external overlays, runtime task generation,"
    )
    print("and full project matcher/ProcessTask equivalence are not evaluated.")
    print(
        "Static image only: no click, transition, plugin, callback, or retry executed."
    )
    return 1 if args.expect_mismatch and not mismatch else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (
        OSError,
        ValueError,
        KeyError,
        RuntimeError,
        subprocess.CalledProcessError,
    ) as exc:
        print(f"probe error: {exc}", file=sys.stderr)
        sys.exit(2)
