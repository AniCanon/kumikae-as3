#!/bin/sh
# Builds the Godot extension for iOS (a device and the Simulator) and puts it beside the test project as
# an .xcframework, the form Godot's iOS export takes.
#
#   tests/build_ios.sh <path to a godot-cpp checkout for Godot 4.7>
#
# KAS3_JOBS in the environment says how many files are compiled at once (8 where it is not set).
#
# Needs Xcode. Nothing is signed here: Godot's export and Xcode do that.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
godot_cpp=$1
if [ -z "$godot_cpp" ]; then
	echo "usage: tests/build_ios.sh <godot-cpp checkout>" >&2
	exit 2
fi
out="$here/tests/godot_project/bin/ios"
rm -rf "$out/kas3_godot.xcframework"
mkdir -p "$out"
libraries=""
for sdk in iphoneos iphonesimulator; do
	build="$here/build-ios-$sdk"
	# Godot's own Simulator library (4.7.2) is built for Intel only, so the Simulator slice has both.
	architectures=arm64
	if [ $sdk = iphonesimulator ]; then
		architectures="arm64;x86_64"
	fi
	mkdir -p "$build"
	cmake -S "$here" -B "$build" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$(xcrun --sdk $sdk --show-sdk-path)" \
		-DCMAKE_OSX_ARCHITECTURES="$architectures" -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_BUILD_TYPE=Release \
		-DKAS3_GODOT_CPP="$godot_cpp" > "$build/build.log" 2>&1
	cmake --build "$build" --target kas3_godot -j"${KAS3_JOBS:-8}" >> "$build/build.log" 2>&1
	libraries="$libraries -library $build/out/libkas3_godot.dylib"
done
# shellcheck disable=SC2086
xcodebuild -create-xcframework $libraries -output "$out/kas3_godot.xcframework"
