# Offline OCR and formation regressions

From any directory, run `python3 /path/to/MaaAssistantArknights/tests/run_offline_regressions.py`. This is the authoritative entry point: it names every required case, fails on missing cases, import errors, failures, unexpected skips, compilation errors, and nonzero subprocess exits. Standard `python3 -m unittest discover` from the repository root also discovers these tests, but does not enforce the required-case manifest and is not the authoritative run. Existing Python tests elsewhere in the repository use unittest `test_*.py`; native CI runs CTest separately.

Requirements: Python 3, Pillow (`python3 -m pip install Pillow`), a C++17 compiler (`CXX` selects it), Boost.Regex headers (`BOOST_INCLUDE_DIR` overrides the macOS MaaDeps vcpkg default), and OpenCV C++ headers and libraries (the probe defaults to macOS MaaDeps; set `MAA_OPENCV_PREFIX` to another prefix). Python OpenCV is not required. See `tools/formation_state_probe.py` for its OpenCV build details. Missing dependencies fail the suite; none is silently skipped. `KR_TASKS_FILE` overrides the KR task JSON for the Sarkaz/JieGarden regression; leave it unset for the normal run.

The only optional case is `test_external_incident_mismatch`, skipped explicitly when `MAA_FORMATION_INCIDENT_IMAGE` is unset or empty. Set it to an external normalized 1280×720 image to test an expected mismatch; a missing, invalid, or non-matching specified image fails. Never copy personal images or real logs into this repository.

These are offline reproductions, not project integration or live game tests. The OCR scripts compile Boost.Regex itself, but manually mirror portions of MAA's equivalence expansion, ordered replacement and required-name matching rather than execute OCRer or the actual recognition pipeline. They cannot establish complete equivalence to project behavior. The formation probe uses OpenCV on synthetic images and selected repository resources, not the complete project matcher, runtime overlays, controller, click/transition, plugin, callback, or retry logic. The trace tests use synthetic lines only. None verifies real OCR, installed resources, or actual in-game behavior.

## Native Korean roguelike replays on macOS arm64

Build the native Debug core with MaaDeps and emulator extras disabled. Supply three external captures in order: a formation overview, the quick-formation panel showing two selected operators, and the Sami reward screen with the leave button. The formation replay uses the two-card panel layout recorded on 2026-11-19. It changes a 3×3 pixel patch in memory to simulate a missed card selection.

```sh
cmake --build build --config Debug --target MaaCore -j 4
sh tests/run_native_roguelike_replays.sh "$overview_png" "$panel_png" "$reward_png"
```

The runner links to the actual matcher and task plugins. A test controller supplies frames and records input without connecting to an emulator. Each case stores its log in the temporary directory printed by the runner. For a separately staged build, set `MAA_CORE_SOURCE` to its matching headers, `MAA_DEBUG_LIB_DIR` to its Debug libraries and `MAA_RESOURCE_DIR` to the directory containing the packaged `resource` folder. The controller probes use `-fno-access-control` only in the test translation units.
