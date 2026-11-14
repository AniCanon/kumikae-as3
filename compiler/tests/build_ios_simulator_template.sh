#!/bin/sh
# Makes a Godot iOS export template whose Simulator library also runs on Apple silicon.
#
# Godot's published template (4.7.2) has a Simulator library for Intel processors only, and current
# Simulators no longer run Intel apps. This builds Godot's unmodified source for the Apple-silicon
# Simulator and puts the result beside the Intel one in a copy of the published template. Nothing in
# Godot is changed, so there is no fork to keep.
#
#   tests/build_ios_simulator_template.sh <godot source at the tag of the installed version> <out.zip>
#
# Needs Xcode, SCons (`pip install scons`) and the published template installed by the Godot editor.
# Set the result as `custom_template/debug` in the iOS export preset.
set -e
source=$1
out=$2
if [ -z "$source" ] || [ -z "$out" ]; then
	echo "usage: tests/build_ios_simulator_template.sh <godot source> <out.zip>" >&2
	exit 2
fi
version=$(godot --version | sed -E 's/^([0-9.]+\.[a-z]+).*/\1/')
published="$HOME/Library/Application Support/Godot/export_templates/$version/ios.zip"
if [ ! -f "$published" ]; then
	echo "no published iOS template for Godot $version at $published" >&2
	exit 1
fi
(cd "$source" && scons platform=ios target=template_debug arch=arm64 ios_simulator=yes -j"$(sysctl -n hw.ncpu)")
work=$(mktemp -d)
unzip -q "$published" -d "$work"
for library in libgodot libgodot_camera; do
	slice="$work/$library.ios.debug.xcframework/ios-arm64_x86_64-simulator/$library.a"
	lipo -create "$slice" "$source/bin/$library.ios.template_debug.arm64.simulator.a" -output "$slice.both"
	mv "$slice.both" "$slice"
done
rm -f "$out"
(cd "$work" && zip -q -r "$out" .)
rm -rf "$work"
echo "written: $out"
