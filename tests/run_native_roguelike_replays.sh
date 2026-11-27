#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 OVERVIEW_PNG FORMATION_PANEL_PNG SAMI_REWARD_PNG" >&2
    exit 2
fi
for frame in "$@"; do
    [ -r "$frame" ] || { echo "Missing frame: $frame" >&2; exit 2; }
done

tests=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=${MAA_CORE_SOURCE:-${tests%/tests}}
libraries=${MAA_DEBUG_LIB_DIR:-"$source_dir/build/bin/Debug"}
deps=${MAA_OPENCV_PREFIX:-"$source_dir/src/MaaUtils/MaaDeps/vcpkg/installed/maa-arm64-osx"}
resources=${MAA_RESOURCE_DIR:-"$source_dir"}
kr="$resources/resource/global/YoStarKR"
scratch=$(mktemp -d)
echo "Native replay logs: $scratch"

compile() {
    "${CXX:-c++}" -std=gnu++20 -DASST_DEBUG -DASST_WITH_EMULATOR_EXTRAS=0 -fno-access-control \
        -I"$source_dir/include" -I"$source_dir/src/MaaCore" -I"$source_dir/src/MaaUtils/include" \
        -I"$source_dir/3rdparty/include" -isystem "$deps/include" -isystem "$deps/include/opencv4" \
        "$tests/$1.cpp" -L"$libraries" -L"$deps/lib" -Wl,-rpath,"$libraries" -Wl,-rpath,"$deps/lib" \
        -lMaaCore -lopencv_world4 -o "$scratch/$1"
}

run_case() {
    program=$1
    label=$2
    shift 2
    mkdir "$scratch/$label"
    if "$scratch/$program" "$scratch/$label" "$resources" "$kr" "$@" > "$scratch/$label/output.log" 2>&1; then
        echo "PASS $label"
    else
        tail -30 "$scratch/$label/output.log" >&2
        exit 1
    fi
}

compile sami_reward_replay
compile formation_reselect_replay
compile encounter_selection_replay
compile encounter_fallback_replay
compile sami_settlement_replay
run_theme() {
    theme=$1
    run_case formation_reselect_replay "$theme-reselect" "$2" "$3" "$theme"
    template="resource/template/Roguelike/$theme/$theme@Roguelike@StageEncounterLeaveConfirm.png"
    button="$kr/$template"
    [ -f "$button" ] || button="$resources/$template"
    run_case encounter_selection_replay "$theme-selection" "$theme" "$button"
    run_case encounter_fallback_replay "$theme-fallback" "$theme"
}

run_case sami_reward_replay sami-synthetic
run_case sami_reward_replay sami-capture "$3"
pids=""
for theme in Phantom Mizuki Sami JieGarden BlackFlow; do
    run_theme "$theme" "$1" "$2" &
    pids="$pids $!"
done
failed=0
for pid in $pids; do
    wait "$pid" || failed=1
done
exit "$failed"
