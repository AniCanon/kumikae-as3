#!/bin/sh
# Builds a size-optimized, single-threaded Godot 4.7 Web GDExtension export template (dlink_enabled=yes)
# from a Godot 4.7 source checkout.
#
#   tools/build_web_template.sh [--2d] [--profile <custom.build>] <godot source checkout> <output.zip>
#
# Stock Godot 4.7 web_dlink_nothreads_release templates are ~42 MiB uncompressed because they include 3D,
# 3D physics, XR, Navigation3D, GDScript, and other modules. Passing --2d trims those subsystems and builds
# with optimize=size lto=full. Measure the resulting export with the assets and modules your game uses.
#
# Needs Emscripten (emcc/em++) matching the Godot 4.7 Web template ABI and SCons on PATH.
set -e

mode_2d=0
profile=""
while [ $# -gt 0 ]; do
	case "$1" in
		--2d)
			mode_2d=1
			shift
			;;
		--profile)
			if [ $# -lt 2 ]; then
				echo "missing argument for --profile" >&2
				exit 2
			fi
			profile=$2
			shift 2
			;;
		-*)
			echo "unknown option: $1" >&2
			exit 2
			;;
		*)
			break
			;;
	esac
done

godot_src=${1:-}
output_zip=${2:-}
if [ -z "$godot_src" ] || [ -z "$output_zip" ]; then
	echo "usage: tools/build_web_template.sh [--2d] [--profile <custom.build>] <godot checkout> <output.zip>" >&2
	exit 2
fi

if ! command -v scons >/dev/null 2>&1; then
	echo "scons is required on PATH to build a custom Godot Web export template." >&2
	exit 2
fi

godot_src=$(cd "$godot_src" && pwd)
mkdir -p "$(dirname "$output_zip")"
out_dir=$(cd "$(dirname "$output_zip")" && pwd)
out_abs="$out_dir/$(basename "$output_zip")"

set -- \
	platform=web \
	target=template_release \
	dlink_enabled=yes \
	threads=no \
	optimize=size \
	lto=full \
	debug_symbols=no \
	minizip=no \
	module_gdscript_enabled=no \
	module_openxr_enabled=no \
	module_webrtc_enabled=no \
	module_multiplayer_enabled=no \
	module_enet_enabled=no \
	module_upnp_enabled=no \
	module_mobile_vr_enabled=no

if [ "$mode_2d" -eq 1 ]; then
	set -- "$@" \
		disable_3d=yes \
		disable_physics_3d=yes \
		disable_navigation_3d=yes \
		disable_xr=yes \
		module_gltf_enabled=no \
		module_fbx_enabled=no \
		module_csg_enabled=no \
		module_gridmap_enabled=no \
		module_meshoptimizer_enabled=no \
		module_vhacd_enabled=no \
		module_xatlas_unwrap_enabled=no
fi

if [ -n "$profile" ]; then
	profile_abs=$(cd "$(dirname "$profile")" && pwd)/$(basename "$profile")
	set -- "$@" "build_profile=$profile_abs"
fi

(cd "$godot_src" && scons -j"${KAS3_JOBS:-8}" "$@")
cp "$godot_src/bin/godot.web.template_release.wasm32.nothreads.dlink.zip" "$out_abs"
echo "Built custom Web export template: $out_abs"
