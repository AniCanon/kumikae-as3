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
#include <mutex>
#include <new>

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <unistd.h>
#endif

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

#if defined(KAS3_TEST_COUNTS)
// How many times generated code asked for the object behind a handle (rt::Host::object_lookups).
uint64_t looked_up = 0;
uint64_t lookups() {
	return looked_up;
}

#endif
// How many of the objects found behind handles (object_behind) Godot has freed. It starts at one: a handle
// nothing was found behind yet has seen none.
std::atomic<uint64_t> gone{ 1 };

// Active native context's freed-instance counter, guarded so worker threads freeing script instances never
// touch Project::get() or dereference a destroyed NativeProgram.
struct ActiveFreed {
	std::mutex lock;
	std::atomic<uint32_t> *counter = nullptr;
};

ActiveFreed &active_freed() {
	static ActiveFreed *state = new ActiveFreed();
	return *state;
}

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
#if defined(KAS3_TEST_COUNTS)
	looked_up++;
#endif
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

// The types of Godot's values that a handle keeps where generated code reaches them: what a method bind
// gives into a new handle (call_kept) and what a method of the type is called on where the handle keeps
// it (rt::value_in). The backend has the same list by name (backend_cpp/names.cpp, in_handle).
bool in_handle(int p_type) {
	return p_type == Variant::DICTIONARY || p_type == Variant::ARRAY || p_type == Variant::CALLABLE || p_type == Variant::RID;
}

// A method bind called on bare values that gives such a value (rt::EngineApi::call_kept): Godot writes it
// over the value a new handle keeps, which starts as the zero of the type the host kept for the place.
void *call_kept(const void *p_bind, void *p_object, const void *const *p_arguments, const void *p_zero) {
	Variant *made = make_handle(*static_cast<const Variant *>(p_zero));
	gdextension_interface::object_method_bind_ptrcall(p_bind, p_object, p_arguments, inside(*made, made->get_type()));
	return made;
}

// What a Dictionary in a handle has under a name, if that is a value with parts of the type asked for
// (rt::EngineApi::keyed): a new handle on it. The key is the text as a String, as the library gives it.
void *keyed(const void *p_handle, const void *p_name, rt::Parts p_type) {
	const Variant *held = static_cast<const Variant *>(p_handle);
	if (!held || held->get_type() != Variant::DICTIONARY) {
		return nullptr;
	}
	const Variant key = String(*static_cast<const StringName *>(p_name));
	bool valid = false;
	const Variant value = held->get(key, &valid);
	return valid && value.get_type() == godot_type(p_type) ? make_handle(value) : nullptr;
}

// Where a handle on a value of the type keeps it, in bytes from the handle's start: 0 unless a handle on
// one starts with the type's number and keeps the value inside itself, as Godot says of one made here.
uint32_t kept_at(int p_type) {
	Variant made;
	GDExtensionCallError error;
	gdextension_interface::variant_construct((GDExtensionVariantType)p_type, made._native_ptr(), nullptr, 0, &error);
	int32_t starts = -1;
	std::memcpy(&starts, made._native_ptr(), sizeof(starts));
	const char *const from = reinterpret_cast<const char *>(made._native_ptr());
	const char *const value = static_cast<const char *>(inside(made, p_type));
	const bool within = error.error == GDEXTENSION_CALL_OK && made.get_type() == p_type && starts == p_type && value >= from + sizeof(starts) &&
			value + sizeof(void *) <= from + sizeof(Variant);
	return within ? static_cast<uint32_t>(value - from) : 0;
}

// A signal emitted by generated code (rt::EngineApi::emit): each value made into Godot's own where Godot
// reads it, on the stack, as the request by name makes it (to_variant), and the emit the host worked out.
bool emit_bare(rt::Context *c, void *p_emitter, void *p_signal, const uint8_t *p_held, const void *const *p_values, uint32_t p_count) {
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
	std::string error;
	const auto ran = emit_signal(*static_cast<const GodotHost::Worked *>(p_emitter), static_cast<const Variant *>(p_signal), pointers, p_count, &error);
	if (ran && !*ran) {
		auto library = c->host->library();
		vm::raise_engine(library, error);
	}
	return ran.has_value();
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
		rt::EngineApi made = { gdextension_interface::object_method_bind_ptrcall, &object_behind, &box, &unbox, &emit_bare, {}, {}, &call_kept, &keyed };
		find_values(&made);
		return made;
	}();
	return &table;
}

// Whether Godot takes or gives a value of the type bare where generated code holds it bare too: a number,
// a Boolean, a value with parts; for a parameter, a name as well, and one of Godot's objects, which
// generated code hands over as the object behind a handle declared to hold exactly the parameter's class.
bool crosses_bare(int p_type, bool p_parameter) {
	return p_type == Variant::BOOL || p_type == Variant::INT || p_type == Variant::FLOAT || has_parts(p_type) ||
			(p_parameter && (p_type == Variant::STRING_NAME || p_type == Variant::OBJECT));
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

void *live_handle_object(void *p_handle) {
	const rt::HandleHead *head = rt::head_of(p_handle);
	return head->seen == gone.load(std::memory_order_relaxed) ? head->object : object_behind(p_handle);
}

static const rt::Program *linked_program = nullptr;

#if defined(IOS_ENABLED)
// Called by the exported app during startup, before Godot creates any script instances.
extern "C" GDE_EXPORT void kas3_ios_set_program(const rt::Program *p_program) {
	linked_program = p_program;
}
#endif

bool NativeProgram::linked() {
	return linked_program != nullptr;
}

std::string NativeProgram::path() {
	if (const char *named = std::getenv("KAS3_PROGRAM_LIBRARY")) {
		return named;
	}
#if defined(IOS_ENABLED) || defined(__EMSCRIPTEN__)
	// The watched project can be on the desktop; its host library cannot run on iOS or the Web.
	return {};
#elif defined(__ANDROID__)
	Dl_info info{};
	if (dladdr(reinterpret_cast<const void *>(&NativeProgram::path), &info) && info.dli_fname) {
		const std::string self(info.dli_fname);
		const size_t slash = self.rfind('/');
		if (slash != std::string::npos) {
			const std::string candidate = self.substr(0, slash + 1) + SharedLibrary::file_name("kas3_program");
			if (access(candidate.c_str(), F_OK) == 0) {
				return candidate;
			}
		}
	}
	return {};
#else
	const String beside = scripts_root().path_join(String::utf8(("bin/" + SharedLibrary::file_name("kas3_program")).c_str()));
	const String file = ProjectSettings::get_singleton()->globalize_path(beside);
	return FileAccess::file_exists(file) ? std::string(file.utf8().get_data()) : std::string();
#endif
}

std::unique_ptr<NativeProgram> NativeProgram::load(const std::string &p_path, const ir::Module &p_module, GodotHost &p_engine, uint32_t p_time_limit,
		bool p_remember, std::string *r_why) {
	std::unique_ptr<NativeProgram> made(new NativeProgram(p_engine));
	const rt::Program *program = linked_program;
	if (!p_path.empty()) {
		if (!made->library_.open(p_path, r_why)) {
			return nullptr;
		}
		using Table = const rt::Program *(*)();
		const Table table = reinterpret_cast<Table>(made->library_.symbol("kas3_program"));
		program = table ? table() : nullptr;
	}
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
	rt::Embedder embedder{ p_engine, p_engine, p_engine, p_engine, &p_module };
	embedder.time_limit = p_time_limit;
	embedder.remember = p_remember;
	made->host_ = std::make_unique<rt::Host>(*program, embedder);
	made->resolve(*program);
	{
		ActiveFreed &state = active_freed();
		const std::lock_guard<std::mutex> held(state.lock);
		state.counter = &made->host_->context.freed;
	}
	return made;
}

// What the host works out once for the program: Godot's names, and for each place generated code calls
// the engine directly, what GodotHost's own working out of the same instruction found (godot_prepared.cpp).
void NativeProgram::resolve(const rt::Program &p_program) {
	rt::Host &host = *host_;
	host.context.engine = api();
	host.context.script_declarations = &engine_.declarations_current;
	host.context.gone = &gone;
#if defined(KAS3_TEST_COUNTS)
	host.object_lookups = &lookups;
#endif
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
		const bool ready = resolve_use(thunk, in, &resolved);
#if defined(KAS3_TEST_COUNTS)
		if (!ready) {
			host.unresolved.push_back(i);
		}
#else
		(void)ready;
#endif
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
	// What a method gives in a handle is made where a new handle keeps it (call_kept).
	const bool kept = in_handle(worked->result);
	bool bare = worked->result == NO_VALUE || crosses_bare(worked->result, false) || kept;
	for (const int parameter : worked->parameters) {
		bare = bare && crosses_bare(parameter, true);
	}
	// A method's arguments follow what it is called on, in the instruction's list; an accessor is given
	// all it takes.
	const size_t given = p_in.op == ir::Op::HostCall ? p_thunk.argument_lists[p_in.c].size() - 1 : worked->parameters.size();
	const size_t missing = worked->parameters.size() - std::min(worked->parameters.size(), given);
	if (!bare || missing > rt::MOST_DEFAULTS ||
			(missing > 0 && !defaults_of(host.module.engine_names[p_in.b], worked->parameters.size(), given, r_resolved->defaults))) {
		return false;
	}
	r_resolved->bind = const_cast<void *>(worked->bound);
	if (kept) {
		Variant *zero = make_handle(worked->zero);
		singletons_.push_back(zero);
		r_resolved->handle = zero;
	}
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
			if (!gives || !std::all_of(p_worked.parameters.begin(), p_worked.parameters.end(), bare)) {
				break;
			}
			if (has_parts(p_worked.receiver)) {
				r_resolved->bind = reinterpret_cast<void *>(p_worked.builtin);
			} else if (const uint32_t at = !p_worked.is_static && in_handle(p_worked.receiver) ? kept_at(p_worked.receiver) : 0) {
				// A method of a value a handle keeps, called where the handle keeps it.
				r_resolved->bind = reinterpret_cast<void *>(p_worked.builtin);
				r_resolved->type = p_worked.receiver;
				r_resolved->at = at;
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
	if (host_) {
		ActiveFreed &state = active_freed();
		const std::lock_guard<std::mutex> held(state.lock);
		if (state.counter == &host_->context.freed) {
			state.counter = nullptr;
		}
	}
	// The host first: what it lets go of is freed by the library's own functions.
	host_.reset();
	for (void *handle : singletons_) {
		engine_.drop(handle);
	}
	names_.clear();
	library_.close();
}

void notify_instance_freed() {
	ActiveFreed &state = active_freed();
	const std::lock_guard<std::mutex> held(state.lock);
	if (state.counter) {
		state.counter->fetch_add(1, std::memory_order_relaxed);
	}
}

} // namespace kas3
