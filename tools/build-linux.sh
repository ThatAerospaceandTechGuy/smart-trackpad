#!/bin/sh
# Portable build of the Linux binary without CMake, for machines that only have
# a compiler (and for CI smoke tests).  CMake is the supported path.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build-linux}
mkdir -p "$out"

# Every path is quoted when the command is assembled, so a repository path with
# spaces works.
common="src/common/Config.cpp src/common/ContactTracker.cpp
src/common/CrashHandlerPosix.cpp src/common/GestureEngine.cpp
src/common/HidDecode.cpp src/common/IniFile.cpp src/common/Log.cpp
src/common/PathCompare.cpp src/common/Platform.cpp src/common/Text.cpp"

linux="src/linux/AppLinux.cpp src/linux/AudioLinux.cpp
src/linux/BrightnessLinux.cpp src/linux/ConfigWatcherLinux.cpp
src/linux/EvdevDecode.cpp src/linux/InputEvdev.cpp src/linux/OsdLinux.cpp src/linux/ZoneTestLinux.cpp
src/linux/main.cpp"

cxx=${CXX:-g++}
command="$cxx -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread"
command="$command -DSMARTTRACKPAD_VERSION='L\"1.3.0\"'"
command="$command -DSMARTTRACKPAD_VERSION_TEXT='\"1.3.0\"'"
command="$command -DSMARTTRACKPAD_HAVE_X11=1 -I'$root/src' -I'$root/src/common'"
command="$command -I'$root/src/linux'"

for file in $common $linux; do
    command="$command '$root/$file'"
done

command="$command -lX11 -o '$out/smarttrackpad'"
eval "$command"

echo "built $out/smarttrackpad"
