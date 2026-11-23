#!/bin/sh
set -eu

if [ "$#" -ne 0 ] && [ "$#" -ne 2 ]; then
    echo "Usage: $0 [LEVEL_SCREEN_PNG DISPLAYED_LEVEL]" >&2
    exit 2
fi
tests=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=${MAA_CORE_SOURCE:-${tests%/tests}}
deps=${MAA_OPENCV_PREFIX:-"$source_dir/src/MaaUtils/MaaDeps/vcpkg/installed/maa-arm64-osx"}
libraries=${MAA_DEBUG_LIB_DIR:-"$source_dir/build/bin/Debug"}
resources=${MAA_RESOURCE_DIR:-"$source_dir"}
scratch=$(mktemp -d)
echo "Native replay logs: $scratch"
"${CXX:-c++}" -std=gnu++20 -DASST_DEBUG -DASST_WITH_EMULATOR_EXTRAS=0 -fno-access-control \
    -I"$source_dir/include" -I"$source_dir/src/MaaCore" -I"$source_dir/src/MaaUtils/include" \
    -I"$source_dir/3rdparty/include" -isystem "$deps/include" -isystem "$deps/include/opencv4" \
    "$tests/oper_progress_level_replay.cpp" -L"$libraries" -L"$deps/lib" \
    -Wl,-rpath,"$libraries" -Wl,-rpath,"$deps/lib" -lMaaCore -lopencv_world4 -o "$scratch/replay"
if "$scratch/replay" "$scratch" "$resources" "$resources/resource/global/YoStarKR" "$@" > "$scratch/output.log" 2>&1; then
    echo "PASS operator level replay"
else
    tail -30 "$scratch/output.log" >&2
    exit 1
fi
