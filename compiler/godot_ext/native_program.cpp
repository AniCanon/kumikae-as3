#include "native_program.h"

#include "godot_calls.h"
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
#include <new>

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

// How many times generated code asked for the object behind a handle (rt::Host::object_lookups).
uint64_t looked_up = 0;
uint64_t lookups() {
	return looked_up;
}

// How many of the objects found behind handles (object_behind) Godot has freed. It starts at one: a handle
// nothing was found behind yet has seen none.
std::atomic<uint64_t> gone{ 1 };

// Godot tells whoever keeps a binding on an object when the object ends, as it tells C# and godot-cpp of
// theirs: this one keeps nothing but the count. Under a token of its own, so that it is beside godot-cpp's
// binding and not in its place. The extension is not one Godot reloads, and must not be made one
// (`reloadable` in kas3.gdextension, which tests/run_godot.py holds every such file to): its bindings last
// as long as it, and a callback into a library that has gone would be a crash.
char watch_token = 0;
void *watched(void *p_token, void *) {
	return p_token;
}
void watched_freed(void *, void *, void *) {
	gone.fetch_add(1, std::memory_order_relaxed);
}
const GDExtensionInstanceBindingCallbacks WATCH = { &watched, &watched_freed, nullptr };

// The object behind a handle: by its id, so that one Godot has freed is null. Asked a second time of one
// handle, the object is kept in the handle's head with what the count of objects freed read before the
// look, and Godot is asked to say when the object ends: until the count moves, generated code reads the
// object there (rt::object_of) and asks nothing. Godot takes an object out of those found by their ids,
// then tells of its end, with nothing of a script's run between (Object::~Object): a freed object is
// never read from a head whose count still matches.
void *object_behind(void *p_handle) {
	looked_up++;
	const Variant *held = static_cast<const Variant *>(p_handle);
	if (!held || held->get_type() != Variant::OBJECT) {
		return nullptr;
	}
	const uint64_t before = gone.load(std::memory_order_relaxed);
	void *const object = gdextension_interface::object_get_instance_from_id(gdextension_interface::variant_get_object_instance_id(held->_native_ptr()));
	rt::HandleHead *const head = rt::head_of(p_handle);
	if (!object) {
		return nullptr;
	}
	if (!head->asked) {
		head->asked = 1; // A handle used once costs no binding.
		return object;
	}
	gdextension_interface::object_get_instance_binding(object, &watch_token, &WATCH);
	head->object = object;
	head->seen = before;
	return object;
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
		const void *value = inside(*held, type);
		// The small ones are copied where they are: a size known only here would be a call for each.
		switch (PARTS_SIZES[static_cast<size_t>(p_type)]) {
			case 8:
				std::memcpy(r_value, value, 8);
				break;
			case 12:
				std::memcpy(r_value, value, 12);
				break;
			case 16:
				std::memcpy(r_value, value, 16);
				break;
			default:
				std::memcpy(r_value, value, PARTS_SIZES[static_cast<size_t>(p_type)]);
				break;
		}
	}
}

// A signal emitted by generated code (rt::EngineApi::emit): each value made into Godot's own where Godot
// reads it, on the stack, as the request by name makes it (to_variant), and the emit the host worked out.
bool emit_bare(void *p_emitter, void *p_signal, const uint8_t *p_held, const void *const *p_values, uint32_t p_count) {
	if (p_count >= MOST_OPERANDS) {
		return false;
	}
	Values values;
	// A number or a Boolean as a Variant owns nothing: it is made here and never given back to Godot.
	alignas(Variant) unsigned char plain[MOST_OPERANDS][sizeof(Variant)];
	const Variant *pointers[MOST_OPERANDS + 1];
	for (uint32_t i = 0; i < p_count; i++) {
		const Variant *handle = nullptr;
		switch (p_held[i]) {
			case rt::GIVEN_WHOLE:
				pointers[i + 1] = new (plain[i]) Variant(*static_cast<const int64_t *>(p_values[i]));
				break;
			case rt::GIVEN_NUMBER:
				pointers[i + 1] = new (plain[i]) Variant(*static_cast<const double *>(p_values[i]));
				break;
			case rt::GIVEN_FLAG:
				pointers[i + 1] = new (plain[i]) Variant(*static_cast<const uint8_t *>(p_values[i]) != 0);
				break;
			case rt::GIVEN_HANDLE:
				// A freed object goes to Godot as what it is to the script: null.
				handle = static_cast<const Variant *>(p_values[i]);
				pointers[i + 1] = handle && !is_freed(*handle) ? handle : new (values.room()) Variant();
				break;
			default: {
				// A value with parts: Godot's own maker of a Variant from the bare value of that type.
				static GDExtensionVariantFromTypeConstructorFunc makers[PARTS] = {};
				const size_t parts = static_cast<size_t>(p_held[i] - rt::GIVEN_PARTS);
				if (!makers[parts]) {
					makers[parts] = gdextension_interface::get_variant_from_type_constructor(
							(GDExtensionVariantType)godot_type(static_cast<rt::Parts>(parts)));
				}
				void *const room = values.room();
				makers[parts](room, const_cast<void *>(p_values[i]));
				pointers[i + 1] = static_cast<const Variant *>(room);
				break;
			}
		}
	}
	return emit_signal(*static_cast<const GodotHost::Worked *>(p_emitter), static_cast<const Variant *>(p_signal), pointers, p_count);
}

// Godot's own entry for a call on bare values is handed over as it is: generated code calls it with nothing
// between, as C# does.
// Where a handle keeps each value with parts, as Godot itself says of a handle on one made here: inside the
// handle for the smaller types, by a number that the handle starts with. If any handle does not start with
// its type's number, nothing is taken for known, and every read is the host's (unbox).
void find_values(rt::EngineApi *r_table) {
	bool numbered = true;
	for (size_t i = 0; i < PARTS; i++) {
		const int type = godot_type(static_cast<rt::Parts>(i));
		Variant made;
		GDExtensionCallError error;
		gdextension_interface::variant_construct((GDExtensionVariantType)type, made._native_ptr(), nullptr, 0, &error);
		int32_t starts = -1;
		std::memcpy(&starts, made._native_ptr(), sizeof(starts));
		numbered = numbered && error.error == GDEXTENSION_CALL_OK && made.get_type() == type && starts == type;
		const char *const from = reinterpret_cast<const char *>(made._native_ptr());
		const char *const value = static_cast<const char *>(inside(made, type));
		const bool within = value >= from + sizeof(starts) && value + PARTS_SIZES[i] <= from + sizeof(Variant);
		r_table->value_at[i] = within ? static_cast<uint8_t>(value - from) : 0;
		r_table->value_type[i] = type;
	}
	if (!numbered) {
		std::memset(r_table->value_at, 0, sizeof(r_table->value_at));
	}
}

const rt::EngineApi *api() {
	static const rt::EngineApi table = [] {
		rt::EngineApi made = { gdextension_interface::object_method_bind_ptrcall, &object_behind, &box, &unbox, &emit_bare, {}, {} };
		find_values(&made);
		return made;
	}();
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
	const String beside = scripts_root().path_join(String::utf8(("bin/" + SharedLibrary::file_name("kas3_program")).c_str()));
	const String file = ProjectSettings::get_singleton()->globalize_path(beside);
	return FileAccess::file_exists(file) ? std::string(file.utf8().get_data()) : std::string();
}

std::unique_ptr<NativeProgram> NativeProgram::load(const std::string &p_path, const ir::Module &p_module, GodotHost &p_engine, std::string *r_why) {
	std::unique_ptr<NativeProgram> made(new NativeProgram(p_engine));
	if (!made->library_.open(p_path, r_why)) {
		return nullptr;
	}
	using Table = const rt::Program *(*)();
	const Table table = reinterpret_cast<Table>(made->library_.symbol("kas3_program"));
	const rt::Program *program = table ? table() : nullptr;
	if (!program) {
		*r_why = "it has no kas3_program";
		return nullptr;
	}
	if (!rt::same_layout(*program)) {
		*r_why = rt::OTHER_LAYOUT;
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
	host.context.gone = &gone;
	host.object_lookups = &lookups;
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
		if (!resolve_use(thunk, in, &resolved)) {
			host.unresolved.push_back(i);
		}
	}
}

bool NativeProgram::resolve_use(const ir::Function &p_thunk, const ir::Instruction &p_in, rt::Resolved *r_resolved) {
	rt::Host &host = *host_;
	if (p_in.op == ir::Op::HostSingleton) {
		std::string error;
		void *handle = engine_.singleton(host.module.engine_names[p_in.b].name, &error);
		if (handle) {
			singletons_.push_back(handle);
			r_resolved->handle = handle;
			r_resolved->object = object_behind(handle);
		}
		return handle != nullptr;
	}
	const GodotHost::Worked *worked = static_cast<const GodotHost::Worked *>(engine_.prepare(host.module, p_thunk, p_in));
	using What = GodotHost::Worked::What;
	if (worked && (worked->what == What::Operator || worked->what == What::Builtin || worked->what == What::FieldGet ||
						  worked->what == What::FieldSet || worked->what == What::Constant)) {
		return resolve_value(*worked, r_resolved);
	}
	if (worked && worked->what == What::Emit) {
		// A signal's emit(): what was worked out is itself what generated code hands back (EngineApi::emit).
		r_resolved->bind = const_cast<GodotHost::Worked *>(worked);
		return true;
	}
	if (!worked || !worked->bound || worked->is_static || worked->index >= 0 ||
			(worked->what != What::Bound && worked->what != What::Getter && worked->what != What::Setter)) {
		return false;
	}
	bool bare = worked->result == NO_VALUE || crosses_bare(worked->result, false);
	for (const int parameter : worked->parameters) {
		bare = bare && crosses_bare(parameter, true);
	}
	// A method's arguments follow what it is called on, in the instruction's list; an accessor is given
	// all it takes.
	const size_t given = p_in.op == ir::Op::HostCall ? p_thunk.argument_lists[p_in.c].size() - 1 : worked->parameters.size();
	const size_t missing = worked->parameters.size() - std::min(worked->parameters.size(), given);
	if (!bare || missing > 2 ||
			(missing > 0 && !defaults_of(host.module.engine_names[p_in.b], worked->parameters.size(), given, r_resolved->defaults))) {
		return false;
	}
	r_resolved->bind = const_cast<void *>(worked->bound);
	return true;
}

// Godot's own code for one of its value types, where everything that crosses is held bare by generated
// code as Godot holds it: a number, a Boolean, a value with parts. A constant is kept as a handle on it.
bool NativeProgram::resolve_value(const GodotHost::Worked &p_worked, rt::Resolved *r_resolved) {
	using What = GodotHost::Worked::What;
	const auto bare = [](int p_type) { return p_type == Variant::BOOL || p_type == Variant::INT || p_type == Variant::FLOAT || has_parts(p_type); };
	const bool gives = p_worked.result == NO_VALUE || bare(p_worked.result);
	switch (p_worked.what) {
		case What::Operator:
			if (bare(p_worked.operands[0]) && (p_worked.operands[1] == Variant::NIL || bare(p_worked.operands[1])) && gives) {
				r_resolved->bind = reinterpret_cast<void *>(p_worked.evaluator);
			}
			break;
		case What::Builtin:
			if (has_parts(p_worked.receiver) && gives && std::all_of(p_worked.parameters.begin(), p_worked.parameters.end(), bare)) {
				r_resolved->bind = reinterpret_cast<void *>(p_worked.builtin);
			}
			break;
		case What::FieldGet:
		case What::FieldSet:
			if (has_parts(p_worked.receiver) && bare(p_worked.result)) {
				r_resolved->bind = p_worked.what == What::FieldGet ? reinterpret_cast<void *>(p_worked.getter) : reinterpret_cast<void *>(p_worked.setter);
			}
			break;
		case What::Constant:
			if (has_parts(p_worked.value.get_type())) {
				Variant *kept = make_handle(p_worked.value);
				singletons_.push_back(kept);
				r_resolved->handle = kept;
			}
			break;
		default:
			break;
	}
	return r_resolved->bind != nullptr || r_resolved->handle != nullptr;
}

NativeProgram::~NativeProgram() {
	// The host first: what it lets go of is freed by the library's own functions.
	host_.reset();
	for (void *handle : singletons_) {
		engine_.drop(handle);
	}
	names_.clear();
	library_.close();
}

} // namespace kas3
