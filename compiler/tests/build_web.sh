#!/bin/sh
# Builds the Godot extension for the Web (WebAssembly, single-threaded) and puts it beside the test project
# where Godot's Web export (with GDExtension support and threads off) expects it.
#
#   tests/build_web.sh <path to a godot-cpp checkout for Godot 4.7>
#
# KAS3_JOBS in the environment says how many files are compiled at once (8 where it is not set).
# Needs Emscripten (emcmake / emcc) on PATH.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
godot_cpp=$1
if [ -z "$godot_cpp" ]; then
	echo "usage: tests/build_web.sh <godot-cpp checkout>" >&2
	exit 2
fi
godot_cpp=$(cd "$godot_cpp" && pwd)

# Emscripten needs Python 3.10+. Prefer Homebrew Python if the default python3 is older (macOS CLT is 3.9).
if [ -z "${EMSDK_PYTHON:-}" ] && [ -x /opt/homebrew/bin/python3 ]; then
	export EMSDK_PYTHON=/opt/homebrew/bin/python3
fi

build="$here/build-web"
out="$here/tests/godot_project/bin/web"
mkdir -p "$build" "$out"

if [ -z "${EM_CACHE:-}" ]; then
	export EM_CACHE="$build/emcache"
fi

emcmake cmake -S "$here" -B "$build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PROJECT_INCLUDE="$godot_cpp/cmake/emsdkHack.cmake" \
	-DKAS3_GODOT_CPP="$godot_cpp" \
	-DKAS3_GODOT_OUTPUT="$out" \
	-DKAS3_WEB_THREADS=OFF
cmake --build "$build" --target kas3_godot test_web_runtime -j"${KAS3_JOBS:-8}"
ctest --test-dir "$build" --output-on-failure -R '^web_runtime$'
echo "Built $out/libkas3_godot.wasm"
