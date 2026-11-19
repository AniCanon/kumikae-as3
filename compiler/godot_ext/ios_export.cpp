#include "ios_export.h"

#if defined(MACOS_ENABLED)
#include "play_build.h"
#include "play_cache.h"
#include "sources.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include <filesystem>

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

String build_objects(const kas3::ir::Module &p_module, const String &p_destination, bool p_debug) {
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

} // namespace

bool AS3IOSExport::_supports_platform(const Ref<EditorExportPlatform> &p_platform) const {
	return p_platform.is_valid() && p_platform->get_os_name() == "iOS";
}

TypedArray<Dictionary> AS3IOSExport::_get_export_options(const Ref<EditorExportPlatform> &p_platform) const {
	TypedArray<Dictionary> options;
	if (_supports_platform(p_platform)) {
		Dictionary option;
		Dictionary property;
		property["name"] = OPTION;
		property["type"] = Variant::BOOL;
		option["option"] = property;
		option["default_value"] = true;
		options.push_back(option);
	}
	return options;
}

void AS3IOSExport::_export_begin(const PackedStringArray &p_features, bool p_debug, const String &, uint32_t) {
	enabled_ = p_features.has("ios") && bool(get_option(OPTION));
	debug_ = p_debug;
	scripts_.clear();
}

void AS3IOSExport::_export_file(const String &p_path, const String &, const PackedStringArray &) {
	if (!enabled_ || !p_path.ends_with(".as") || p_path.contains("/.") || kas3::excluded_script(p_path.utf8().get_data())) {
		return;
	}
	if (scripts_.empty()) {
		add_apple_embedded_platform_cpp_code(REGISTER_PROGRAM);
		add_apple_embedded_platform_linker_flags("\"$(PROJECT_DIR)/$(PROJECT_NAME)/.kas3/$(PLATFORM_NAME).o\"");
	}
	scripts_.emplace_back(p_path.utf8().get_data());
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
			? build_objects(module, destination, debug_) : String::utf8(diagnostics.c_str());
	if (!error.is_empty()) {
		get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Native AS3", error);
	} else {
		get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_INFO, "ActionScript", "Built native scripts for iOS and the Simulator.");
	}
}

void AS3IOSExport::_export_end() {
	scripts_.clear();
	enabled_ = false;
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
