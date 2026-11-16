#include "native_program.h"

#include "handles.h"
#include "sources.h"
#include "worked.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

using namespace godot;

namespace kas3 {

namespace {

constexpr size_t PARTS = static_cast<size_t>(rt::Parts::Count);
// How many bytes each value with parts is, as the runtime's struct of it (and so as Godot's).
constexpr size_t PARTS_SIZES[PARTS] = { sizeof(rt::Vector2), sizeof(rt::Vector2i), sizeof(rt::Rect2), sizeof(rt::Rect2i), sizeof(rt::Vector3),
	sizeof(rt::Vector3i), sizeof(rt::Transform2D), sizeof(rt::Vector4), sizeof(rt::Vector4i), sizeof(rt::Plane), sizeof(rt::Quaternion),
	sizeof(rt::AABB), sizeof(rt::Basis), sizeof(rt::Transform3D), sizeof(rt::Projection), sizeof(rt::Color) };

// Godot's number for each of the runtime's types with parts, found by name once.
int godot_type(rt::Parts p_type) {
	static const std::array<int, PARTS> types = [] {
		std::array<int, PARTS> found{};
		for (size_t i = 0; i < PARTS; i++) {
			found[i] = value_type(rt::PARTS_NAMES[i]).value_or(Variant::NIL);
		}
		return found;
	}();
	return types[static_cast<size_t>(p_type)];
}

bool has_parts(int p_type) {
	for (size_t i = 0; i < PARTS; i++) {
		if (godot_type(static_cast<rt::Parts>(i)) == p_type) {
			return true;
		}
	}
	return false;
}

// Where a Variant of a type keeps its bare value.
void *inside(Variant &p_value, int p_type) {
	return gdextension_interface::variant_get_ptr_internal_getter((GDExtensionVariantType)p_type)(p_value._native_ptr());
}

// The object behind a handle: by its id, so that one Godot has freed is null.
void *object_behind(void *p_handle) {
	const Variant *held = static_cast<const Variant *>(p_handle);
	if (!held || held->get_type() != Variant::OBJECT) {
		return nullptr;
	}
	return gdextension_interface::object_get_instance_from_id(gdextension_interface::variant_get_object_instance_id(held->_native_ptr()));
}

void *box(rt::Parts p_type, const void *p_value) {
	const int type = godot_type(p_type);
	Variant zero;
	GDExtensionCallError error;
	gdextension_interface::variant_construct((GDExtensionVariantType)type, zero._native_ptr(), nullptr, 0, &error);
	Variant *made = make_handle(zero);
	std::memcpy(inside(*made, type), p_value, PARTS_SIZES[static_cast<size_t>(p_type)]);
	return made;
}

void unbox(const void *p_handle, rt::Parts p_type, void *r_value) {
	Variant *held = const_cast<Variant *>(static_cast<const Variant *>(p_handle));
	const int type = godot_type(p_type);
	if (held && held->get_type() == type) {
		std::memcpy(r_value, inside(*held, type), PARTS_SIZES[static_cast<size_t>(p_type)]);
	}
}

// Godot's own entry for a call on bare values is handed over as it is: generated code calls it with nothing
// between, as C# does.
const rt::EngineApi *api() {
	static const rt::EngineApi table = { gdextension_interface::object_method_bind_ptrcall, &object_behind, &box, &unbox };
	return &table;
}

// Whether Godot takes or gives a value of the type bare where generated code holds it bare too: a number,
// a Boolean, a value with parts; for a parameter, a name as well.
bool crosses_bare(int p_type, bool p_parameter) {
	return p_type == Variant::BOOL || p_type == Variant::INT || p_type == Variant::FLOAT || has_parts(p_type) ||
			(p_parameter && p_type == Variant::STRING_NAME);
}

// What Godot declares for the parameters of a method that a call leaves out, bare, into r_defaults: false
// if it declares none there, or one that is not a number or a Boolean.
bool defaults_of(const ir::EngineName &p_entry, size_t p_parameters, size_t p_given, uint64_t *r_defaults) {
	const StringName owner(String::utf8(p_entry.owner.c_str()));
	const String method = String::utf8(p_entry.name.c_str());
	const TypedArray<Dictionary> methods = ClassDBSingleton::get_singleton()->class_get_method_list(owner, true);
	for (int64_t i = 0; i < methods.size(); i++) {
		const Dictionary each = methods[i];
		if (String(each["name"]) != method) {
			continue;
		}
		const Array declared = each["default_args"];
		for (size_t parameter = p_given; parameter < p_parameters; parameter++) {
			const int64_t at = static_cast<int64_t>(parameter) - (static_cast<int64_t>(p_parameters) - declared.size());
			if (at < 0 || at >= declared.size()) {
				return false;
			}
			const Variant value = declared[at];
			uint64_t &bare = r_defaults[parameter - p_given];
			bare = 0;
			if (value.get_type() == Variant::BOOL) {
				const uint8_t flag = static_cast<bool>(value);
				std::memcpy(&bare, &flag, sizeof(flag));
			} else if (value.get_type() == Variant::INT) {
				const int64_t whole = value;
				std::memcpy(&bare, &whole, sizeof(whole));
			} else if (value.get_type() == Variant::FLOAT) {
				const double number = value;
				std::memcpy(&bare, &number, sizeof(number));
			} else {
				return false;
			}
		}
		return true;
	}
	return false;
}

} // namespace

std::string NativeProgram::path() {
	if (const char *named = std::getenv("KAS3_PROGRAM_LIBRARY")) {
		return named;
	}
#if defined(__APPLE__)
	const String beside = scripts_root().path_join("bin/libkas3_program.dylib");
#else
	const String beside = scripts_root().path_join("bin/libkas3_program.so");
#endif
	const String file = ProjectSettings::get_singleton()->globalize_path(beside);
	return FileAccess::file_exists(file) ? std::string(file.utf8().get_data()) : std::string();
}

std::unique_ptr<NativeProgram> NativeProgram::load(const std::string &p_path, const ir::Module &p_module, GodotHost &p_engine, std::string *r_why) {
	std::unique_ptr<NativeProgram> made(new NativeProgram(p_engine));
	made->library_ = dlopen(p_path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!made->library_) {
		*r_why = dlerror();
		return nullptr;
	}
	using Table = const rt::Program *(*)();
	const Table table = reinterpret_cast<Table>(dlsym(made->library_, "kas3_program"));
	const rt::Program *program = table ? table() : nullptr;
	if (!program) {
		*r_why = "it has no kas3_program";
		return nullptr;
	}
	if (program->fingerprint != ir::fingerprint(p_module) || program->function_count != p_module.functions.size() ||
			program->class_count != p_module.classes.size()) {
		*r_why = "it was built from other sources than the project's scripts are now";
		return nullptr;
	}
	made->host_ = std::make_unique<rt::Host>(*program, rt::Embedder{ p_engine, p_engine, p_engine, p_engine, &p_module });
	made->resolve(*program);
	return made;
}

// What the host works out once for the program: Godot's names, and for each place generated code calls
// the engine directly, what GodotHost's own working out of the same instruction found (godot_prepared.cpp).
void NativeProgram::resolve(const rt::Program &p_program) {
	rt::Host &host = *host_;
	host.context.engine = api();
	names_.reserve(p_program.name_count);
	for (uint32_t i = 0; i < p_program.name_count; i++) {
		const rt::Text &text = p_program.strings[p_program.names[i]];
		names_.emplace_back(String::utf8(text.data, static_cast<int64_t>(text.size)));
		host.names[i] = names_.back()._native_ptr();
	}
	for (uint32_t i = 0; i < p_program.use_count; i++) {
		const ir::Function &thunk = host.module.functions[host.thunk_function(p_program.uses[i])];
		const ir::Instruction &in = thunk.code[0];
		rt::Resolved &resolved = host.resolved[i];
		if (in.op == ir::Op::HostSingleton) {
			std::string error;
			void *handle = engine_.singleton(host.module.engine_names[in.b].name, &error);
			if (handle) {
				singletons_.push_back(handle);
				resolved.handle = handle;
				resolved.object = object_behind(handle);
			}
			continue;
		}
		const GodotHost::Worked *worked = static_cast<const GodotHost::Worked *>(engine_.prepare(host.module, thunk, in));
		using What = GodotHost::Worked::What;
		if (!worked || !worked->bound || worked->is_static || worked->index >= 0 ||
				(worked->what != What::Bound && worked->what != What::Getter && worked->what != What::Setter)) {
			continue;
		}
		bool bare = worked->result == NO_VALUE || crosses_bare(worked->result, false);
		for (const int parameter : worked->parameters) {
			bare = bare && crosses_bare(parameter, true);
		}
		// A method's arguments follow what it is called on, in the instruction's list; an accessor is given
		// all it takes.
		const size_t given = in.op == ir::Op::HostCall ? thunk.argument_lists[in.c].size() - 1 : worked->parameters.size();
		const size_t missing = worked->parameters.size() - std::min(worked->parameters.size(), given);
		if (!bare || missing > 2 || (missing > 0 && !defaults_of(host.module.engine_names[in.b], worked->parameters.size(), given, resolved.defaults))) {
			continue;
		}
		resolved.bind = const_cast<void *>(worked->bound);
	}
}

NativeProgram::~NativeProgram() {
	// The host first: what it lets go of is freed by the library's own functions.
	host_.reset();
	for (void *handle : singletons_) {
		engine_.drop(handle);
	}
	names_.clear();
	if (library_) {
		dlclose(library_);
	}
}

} // namespace kas3
