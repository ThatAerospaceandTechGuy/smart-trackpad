#!/bin/sh
# Builds and runs the five test binaries with nothing but a compiler, for
# machines and CI jobs that do not use CMake.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build-tests}
mkdir -p "$out"

cxx=${CXX:-g++}
flags="-std=c++20 -O1 -Wall -Wextra -Wpedantic -pthread"
status=0

build() {
    name=$1
    shift
    command="$cxx $flags -I'$root/src' -I'$root/src/common'"
    for file in "$@"; do
        command="$command '$root/$file'"
    done
    command="$command -o '$out/$name'"
    eval "$command"
}

build gesture_engine src/common/GestureEngine.cpp tests/GestureEngineTests.cpp
build hid_decode src/common/HidDecode.cpp tests/HidDecodeTests.cpp
build path_compare src/common/PathCompare.cpp tests/PathCompareTests.cpp
build contact_tracker src/common/ContactTracker.cpp tests/ContactTrackerTests.cpp
build config src/common/Config.cpp src/common/IniFile.cpp src/common/Log.cpp \
    src/common/Platform.cpp src/common/Text.cpp tests/ConfigTests.cpp

for name in gesture_engine hid_decode path_compare contact_tracker config; do
    printf '%-18s' "$name"
    if "$out/$name" >"$out/$name.log" 2>&1; then
        echo "ok"
    else
        echo "FAILED"
        cat "$out/$name.log"
        status=1
    fi
done

exit $status
