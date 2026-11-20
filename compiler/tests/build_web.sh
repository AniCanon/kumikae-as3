#!/bin/sh
# Builds the Godot extension for the Web (WebAssembly, single-threaded) and puts it beside the test project
# where Godot's Web export (with GDExtension support and threads off) expects it.
#
#   tests/build_web.sh [--compiler] <path to a godot-cpp checkout for Godot 4.7>
#
# By default this builds the slim runtime-only extension (bin/web/libkas3_godot.wasm), which runs the
# precompiled res://.kas3/program.kas3 module packed by Godot's Web export without embedding the AS3 front
# end or Godot's declaration tables. Pass --compiler to build bin/web/libkas3_godot_compiler.wasm with the
# in-browser AS3 compiler and kas3_web_reload hot-reload API.
#
# KAS3_JOBS in the environment says how many files are compiled at once (8 where it is not set).
# Needs Emscripten (emcmake / emcc) on PATH.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
runtime_only=ON
output_name=libkas3_godot.wasm
build_suffix=""
if [ "${1:-}" = "--compiler" ]; then
	runtime_only=OFF
	output_name=libkas3_godot_compiler.wasm
	build_suffix="-compiler"
	shift
fi
godot_cpp=${1:-}
if [ -z "$godot_cpp" ]; then
	echo "usage: tests/build_web.sh [--compiler] <godot-cpp checkout>" >&2
	exit 2
fi
godot_cpp=$(cd "$godot_cpp" && pwd)

# Emscripten needs Python 3.10+. Prefer Homebrew Python if the default python3 is older (macOS CLT is 3.9).
if [ -z "${EMSDK_PYTHON:-}" ] && [ -x /opt/homebrew/bin/python3 ]; then
	export EMSDK_PYTHON=/opt/homebrew/bin/python3
fi

build="$here/build-web$build_suffix"
out="$here/tests/godot_project/bin/web"
stage="$build/out"
mkdir -p "$build" "$out" "$stage"

if [ -z "${EM_CACHE:-}" ]; then
	export EM_CACHE="$build/emcache"
fi

emcmake cmake -S "$here" -B "$build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PROJECT_INCLUDE="$godot_cpp/cmake/emsdkHack.cmake" \
	-DKAS3_GODOT_CPP="$godot_cpp" \
	-DKAS3_GODOT_OUTPUT="$stage" \
	-DKAS3_WEB_THREADS=OFF \
	-DKAS3_RUNTIME_ONLY="$runtime_only"
cmake --build "$build" --target kas3_godot test_web_runtime -j"${KAS3_JOBS:-8}"
ctest --test-dir "$build" --output-on-failure -R '^web_runtime$'

# Shrink the side module further with Binaryen's wasm-opt when available alongside Emscripten.
wasm_opt=${WASM_OPT:-}
if [ -z "$wasm_opt" ]; then
	if command -v wasm-opt >/dev/null 2>&1; then
		wasm_opt=$(command -v wasm-opt)
	elif command -v em-config >/dev/null 2>&1; then
		binaryen=$(em-config BINARYEN_ROOT)
		if [ -x "$binaryen/bin/wasm-opt" ]; then
			wasm_opt="$binaryen/bin/wasm-opt"
		fi
	fi
fi

if [ -n "$wasm_opt" ]; then
	echo "Optimizing $output_name with $wasm_opt"
	"$wasm_opt" -Oz \
		--enable-bulk-memory --enable-sign-ext --enable-mutable-globals \
		--enable-nontrapping-float-to-int --enable-exception-handling --enable-reference-types \
		"$stage/libkas3_godot.wasm" -o "$out/$output_name"
else
	cp "$stage/libkas3_godot.wasm" "$out/$output_name"
fi

echo "Built $out/$output_name"
