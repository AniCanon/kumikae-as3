#include "ios_export.h"

#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
#include "ir_binary.h"
#include "play_build.h"
#include "play_cache.h"
#include "sources.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <system_error>

using namespace godot;
namespace fs = std::filesystem;

namespace {

constexpr const char *OPTION = "actionscript/native_code";
// The application supplies its own table to the extension before Godot starts loading scripts.
constexpr const char *REGISTER_PROGRAM = R"(
namespace kas3 { namespace rt { struct Program; } }
extern "C" const kas3::rt::Program *kas3_program();
extern "C" void kas3_ios_set_program(const kas3::rt::Program *);
namespace {
struct AS3Program {
	AS3Program() { kas3_ios_set_program(kas3_program()); }
} as3_program;
}
)";

String run(const String &p_tool, const PackedStringArray &p_arguments) {
	Array output;
	if (OS::get_singleton()->execute(p_tool, p_arguments, output, true) == 0) {
		return {};
	}
	return p_tool.get_file() + " failed. " + String("\n").join(PackedStringArray(output));
}

String build_ios_objects(const kas3::ir::Module &p_module, const String &p_destination, bool p_debug) {
	const Ref<DirAccess> temporary = DirAccess::create_temp("kas3-ios");
	if (temporary.is_null()) {
		return "Could not create a temporary folder for the native AS3 sources.";
	}
	const String source = temporary->get_current_dir();
	const std::string error = kas3::play::write_sources(p_module, fs::path(source.utf8().get_data()), p_debug);
	if (!error.empty()) {
		return String::utf8(error.c_str());
	}
	if (DirAccess::make_dir_recursive_absolute(p_destination) != OK) {
		return "Could not create " + p_destination;
	}
	for (const String &sdk : PackedStringArray({ "iphoneos", "iphonesimulator" })) {
		const bool simulator = sdk == "iphonesimulator";
		PackedStringArray args = { "--sdk", sdk, "clang++", "-arch", "arm64" };
		if (simulator) {
			args.append_array(PackedStringArray({ "-arch", "x86_64" }));
		}
		args.push_back(simulator ? "-mios-simulator-version-min=15.0" : "-miphoneos-version-min=15.0");
		args.append_array(String(kas3::play::COMPILE_FLAGS).split(" ", false));
		args.append_array(PackedStringArray({ "-I", source.path_join(kas3::play::HEADERS), "-c", source.path_join("program.cpp"),
				"-o", p_destination.path_join(sdk + String(".o")) }));
		const String why = run("/usr/bin/xcrun", args);
		if (!why.is_empty()) {
			return why;
		}
	}
	return {};
}

std::optional<fs::path> find_ndk_clang() {
	std::vector<fs::path> ndk_roots;
	for (const char *var : { "ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "NDK_HOME" }) {
		if (const char *value = std::getenv(var); value && *value) {
			ndk_roots.emplace_back(value);
		}
	}
	std::vector<fs::path> sdk_roots;
	for (const char *var : { "ANDROID_HOME", "ANDROID_SDK_ROOT" }) {
		if (const char *value = std::getenv(var); value && *value) {
			sdk_roots.emplace_back(value);
		}
	}
	if (const char *home = std::getenv("HOME"); home && *home) {
		sdk_roots.push_back(fs::path(home) / "Library/Android/sdk");
		sdk_roots.push_back(fs::path(home) / "Android/Sdk");
	}
	std::error_code ec;
	for (const fs::path &sdk : sdk_roots) {
		const fs::path ndk_dir = sdk / "ndk";
		if (!fs::is_directory(ndk_dir, ec)) {
			continue;
		}
		std::vector<fs::path> versions;
		for (const fs::directory_entry &entry : fs::directory_iterator(ndk_dir, ec)) {
			if (entry.is_directory(ec)) {
				versions.push_back(entry.path());
			}
		}
		std::sort(versions.rbegin(), versions.rend());
		ndk_roots.insert(ndk_roots.end(), versions.begin(), versions.end());
	}
	const char *suffix = "";
#if defined(_WIN32)
	suffix = ".cmd";
#endif
	for (const fs::path &ndk : ndk_roots) {
		const fs::path prebuilt = ndk / "toolchains/llvm/prebuilt";
		if (!fs::is_directory(prebuilt, ec)) {
			continue;
		}
		for (const fs::directory_entry &host : fs::directory_iterator(prebuilt, ec)) {
			if (!host.is_directory(ec)) {
				continue;
			}
			const fs::path bin = host.path() / "bin";
			for (const char *api : { "24", "21", "26", "28", "29", "30" }) {
				const fs::path clang = bin / (std::string("aarch64-linux-android") + api + "-clang++" + suffix);
				if (fs::is_regular_file(clang, ec)) {
					return clang;
				}
			}
		}
	}
	return std::nullopt;
}

String build_android_library(const fs::path &p_stage, bool p_debug, String *r_library) {
	const std::optional<fs::path> clang = find_ndk_clang();
	if (!clang) {
		return "Android NDK was not found (set ANDROID_NDK_HOME or install an NDK in ANDROID_HOME/ndk).";
	}
	const String root = kas3::scripts_root();
	std::vector<std::string> files;
	kas3::collect_scripts(root, files);
	std::vector<std::string> scripts;
	for (const std::string &path : files) {
		const std::string local = kas3::project_path(path, root);
		if (!kas3::excluded_script(local)) {
			scripts.push_back(local);
		}
	}
	if (scripts.empty()) {
		return {};
	}
	kas3::ir::Module module;
	std::string diagnostics;
	if (!kas3::compile_scripts(std::move(scripts), root, &module, &diagnostics)) {
		return String::utf8(diagnostics.c_str());
	}
	const std::string write_err = kas3::play::write_sources(module, p_stage, p_debug);
	if (!write_err.empty()) {
		return String::utf8(write_err.c_str());
	}
	const fs::path out = p_stage / "libkas3_program.so";
	PackedStringArray args = String(kas3::play::COMPILE_FLAGS).split(" ", false);
	args.append_array(PackedStringArray({ "-shared", "-fPIC", "-fno-math-errno", "-I",
			String::utf8((p_stage / kas3::play::HEADERS).string().c_str()),
			String::utf8((p_stage / "program.cpp").string().c_str()), "-o", String::utf8(out.string().c_str()) }));
	const String why = run(String::utf8(clang->string().c_str()), args);
	if (!why.is_empty()) {
		return why;
	}
	*r_library = String::utf8(out.string().c_str());
	return {};
}

} // namespace

bool AS3IOSExport::_supports_platform(const Ref<EditorExportPlatform> &p_platform) const {
	if (p_platform.is_null()) {
		return false;
	}
	const String os = p_platform->get_os_name();
#if defined(MACOS_ENABLED)
	return os == "iOS" || os == "Android" || os == "Web";
#else
	return os == "Android" || os == "Web";
#endif
}

TypedArray<Dictionary> AS3IOSExport::_get_export_options(const Ref<EditorExportPlatform> &p_platform) const {
	TypedArray<Dictionary> options;
	if (!p_platform.is_null() && _supports_platform(p_platform) && p_platform->get_os_name() != "Web") {
		Dictionary option;
		Dictionary property;
		property["name"] = OPTION;
		property["type"] = Variant::INT;
		property["hint"] = PROPERTY_HINT_ENUM;
		property["hint_string"] = "Automatic:2,Interpreter:0,Native:1";
		option["option"] = property;
		option["default_value"] = 2;
		options.push_back(option);
	}
	return options;
}

void AS3IOSExport::_export_begin(const PackedStringArray &p_features, bool p_debug, const String &, uint32_t) {
	ios_ = p_features.has("ios");
	android_ = p_features.has("android");
	web_ = p_features.has("web");
	enabled_ = false;
	if (ios_ || android_) {
		// Values 0 and 1 also preserve the choices saved by the old boolean option.
		const int mode = get_option(OPTION);
		enabled_ = mode == 1 || (mode == 2 && !p_debug);
	}
	debug_ = p_debug;
	scripts_.clear();
	android_temp_.unref();
	if (android_ && enabled_) {
		android_temp_ = DirAccess::create_temp("kas3-android");
		if (android_temp_.is_null()) {
			get_export_platform()->add_message(
					EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Native AS3", "Could not create a temporary folder for Android native sources.");
			return;
		}
		const fs::path stage(android_temp_->get_current_dir().utf8().get_data());
		String library;
		const String error = build_android_library(stage, debug_, &library);
		if (!error.is_empty()) {
			get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Native AS3", error);
		} else if (!library.is_empty()) {
			add_shared_object(library, PackedStringArray({ "arm64-v8a" }), "");
			get_export_platform()->add_message(
					EditorExportPlatform::EXPORT_MESSAGE_INFO, "ActionScript", "Built native scripts for Android ARM64.");
		}
	}
}

void AS3IOSExport::_export_file(const String &p_path, const String &, const PackedStringArray &) {
	if ((!web_ && !(ios_ && enabled_)) || !p_path.ends_with(".as") || p_path.contains("/.") || kas3::excluded_script(p_path.utf8().get_data())) {
		return;
	}
	if (web_) {
		// Keep the script resource at its original path for scenes and the optional browser compiler.
		// Loading each .as through scene customization would try to attach even plain AS3 classes.
		Ref<FileAccess> source = FileAccess::open(p_path, FileAccess::READ);
		if (source.is_null()) {
			get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Native AS3", "Cannot read " + p_path);
			return;
		}
		add_file(p_path, source->get_buffer(source->get_length()), false);
		skip();
	}
	if (ios_ && scripts_.empty()) {
		add_apple_embedded_platform_cpp_code(REGISTER_PROGRAM);
		add_apple_embedded_platform_linker_flags("\"$(PROJECT_DIR)/$(PROJECT_NAME)/.kas3/$(PLATFORM_NAME).o\"");
	}
	scripts_.emplace_back(p_path.utf8().get_data());
}

// Godot flushes added files after scene customization ends. At this point _export_file has seen
// exactly the paths chosen by the preset, including its include/exclude filters and dependencies.
bool AS3IOSExport::_begin_customize_scenes(const Ref<EditorExportPlatform> &, const PackedStringArray &) const {
	return web_;
}

void AS3IOSExport::_end_customize_scenes() {
	if (!web_ || scripts_.empty()) {
		return;
	}
	kas3::ir::Module module;
	std::string diagnostics;
	if (!kas3::compile_scripts(std::move(scripts_), "res://", &module, &diagnostics)) {
		get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Native AS3", String::utf8(diagnostics.c_str()));
		return;
	}
	const std::vector<uint8_t> encoded = kas3::ir::write_binary(module);
	PackedByteArray bytes;
	if (bytes.resize(static_cast<int64_t>(encoded.size())) != OK) {
		get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Native AS3", "No memory for the Web program.");
		return;
	}
	std::memcpy(bytes.ptrw(), encoded.data(), encoded.size());
	add_file(kas3::PRECOMPILED_MODULE, bytes, false);
	get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_INFO, "ActionScript", "Packed precompiled AS3 module for Web.");
}

void AS3IOSExport::_end_generate_apple_embedded_project(const String &p_path, bool) {
	if (scripts_.empty()) {
		return;
	}
	// The pack now contains exactly the files seen in _export_file. Godot has written the Xcode project
	// and will only start archiving after this callback. Xcode links the object for the selected SDK.
	const String destination = ProjectSettings::get_singleton()->globalize_path(p_path.get_basename()).path_join(".kas3");
	kas3::ir::Module module;
	std::string diagnostics;
	const String error = kas3::compile_scripts(std::move(scripts_), "res://", &module, &diagnostics)
			? build_ios_objects(module, destination, debug_)
			: String::utf8(diagnostics.c_str());
	if (!error.is_empty()) {
		get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Native AS3", error);
	} else {
		get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_INFO, "ActionScript", "Built native scripts for iOS and the Simulator.");
	}
}

void AS3IOSExport::_export_end() {
	scripts_.clear();
	android_temp_.unref();
	enabled_ = false;
	ios_ = false;
	android_ = false;
	web_ = false;
}

void AS3ExportEditor::_enter_tree() {
	exporter_.instantiate();
	add_export_plugin(exporter_);
}

void AS3ExportEditor::_exit_tree() {
	remove_export_plugin(exporter_);
	exporter_.unref();
}
#endif
