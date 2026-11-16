// A value put into words, as functions of plain values (vm/library.h): String(x) of an untyped value, an
// object's text (its own toString(), a vector's elements, "[object Name]"), and ECMAScript's ToPrimitive.
// An object that speaks for itself runs the program, by its function's number.
#include "library.h"

#include "date_time.h"
#include "regex.h"
#include "stack.h"
#include "table.h"
#include "text_units.h"

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::NONE;

namespace {

// Vectors put into words inside one another's words (an Array in an Array, or one that holds itself)
// nest at most this deep, by recursion in C++; one more is Error #1023, as Flash's stack check gives.
// A level takes about 270 bytes of stack in a release build (measured): 500 fit in a third of the
// 512 KB a secondary thread gets on macOS.
constexpr uint32_t MAX_TEXT_DEPTH = 500;

bool object_as_text(Library &p_library, Object *p_object, std::string *r_text);

// What the object's method gives when called with no arguments, as text, added to r_text. False if the
// method failed, or needs an argument (AS3's ArgumentError #1063).
bool said_by(Library &p_library, uint32_t p_method, Object *p_object, std::string *r_text) {
	const ir::Function &method = p_library.module().functions[p_method];
	Slot self[1];
	const std::optional<size_t> given = function_arguments(p_library, p_method, p_object, 0, nullptr, self);
	if (!given) {
		return false;
	}
	Slot result;
	result.bits = 0;
	retain(p_object); // Held while it speaks, for the reason element_text() gives.
	const bool called = p_library.runner.run(p_method, self, *given, &result);
	p_library.heap.release_object(p_object);
	if (!called) {
		return false;
	}
	const Slot said_value = method.result != Kind::Void ? to_any(p_library, method.result, result) : any::undefined();
	p_library.heap.drop(method.result, result);
	std::string said;
	const bool done = any_text(p_library, said_value, &said);
	p_library.heap.drop_any(said_value);
	if (done) {
		append_text(r_text, said);
	}
	return done;
}

// One element of a vector of that kind as text, added to r_text. False if what it said failed.
bool element_text(Library &p_library, Kind p_kind, Slot p_element, std::string *r_text) {
	switch (p_kind) {
		case Kind::I32:
			*r_text += std::to_string(p_element.i);
			return true;
		case Kind::U32:
			*r_text += std::to_string(p_element.u);
			return true;
		case Kind::I64:
			*r_text += std::to_string(p_element.l);
			return true;
		case Kind::F64:
			*r_text += number_to_string(p_element.d);
			return true;
		case Kind::Bool:
			*r_text += p_element.b ? "true" : "false";
			return true;
		case Kind::Str:
			append_text(r_text, p_element.p ? static_cast<Str *>(p_element.p)->view() : std::string_view("null"));
			return true;
		case Kind::Any: {
			// Held while it speaks, for the reason below.
			const Slot held = p_library.heap.share(Kind::Any, p_element);
			std::string said;
			const bool done = any_text(p_library, held, &said);
			p_library.heap.drop_any(held);
			if (!done) {
				return false;
			}
			append_text(r_text, said);
			return true;
		}
		case Kind::Obj: {
			// Held while it speaks: what it says may run the program, which may take it out of
			// this vector, and then nothing else would be holding it.
			Object *object = static_cast<Object *>(p_element.p);
			retain(object);
			const bool said = object_as_text(p_library, object, r_text);
			p_library.heap.release_object(object);
			return said;
		}
		default:
			// A handle of its own, for the same reason: Godot's text for an object can run a script.
			return engine_text(p_library, p_element.p, r_text);
	}
}

bool element_locale_text(Library &p_library, Kind p_kind, Slot p_element, std::string *r_text) {
	const ir::Module &module = p_library.module();
	Object *object = p_kind == Kind::Obj ? static_cast<Object *>(p_element.p) : p_kind == Kind::Any ? held_object(p_element) : nullptr;
	if (object && object->klass == module.date_class) {
		*r_text += date_text(p_library, object, ir::DateText::Locale);
		return true;
	}
	if (!object || module.classes[object->klass].element == Kind::Void) {
		// A class's own toLocaleString() is what avmplus asks each element for; Object's is its toString().
		const uint32_t own = object ? find_method(p_library, object->klass, "toLocaleString") : NONE;
		const bool callable = own != NONE && module.functions[own].is_public && !module.functions[own].is_static;
		return callable ? said_by(p_library, own, object, r_text) : element_text(p_library, p_kind, p_element, r_text);
	}
	retain(object); // Held while its elements speak, for the reason element_text() gives.
	const bool said = vector_text(p_library, static_cast<Vector *>(object), ",", r_text, true);
	p_library.heap.release_object(object);
	return said;
}

// An Object's or a Dictionary's own toString, when that property holds a function: *r_own, and what it
// gives as text added to r_text. False if it raised.
bool table_text(Library &p_library, Object *p_table, std::string *r_text, bool *r_own) {
	const std::optional<uint32_t> at = entries_of(p_table)->find_text("toString");
	const Slot value = at ? entries_of(p_table)->value_at(*at) : any::undefined();
	Object *function = held_object(value);
	*r_own = function && function->klass == p_library.module().function_class;
	if (!*r_own) {
		return true;
	}
	retain(function);
	Slot said = any::undefined();
	const bool called = function_call(p_library, function, nullptr, 0, &said);
	p_library.heap.release_object(function);
	if (!called) {
		return false;
	}
	const bool done = any_text(p_library, said, r_text);
	p_library.heap.drop_any(said);
	return done;
}

// An object as String() gives it: a vector's elements, a class's own toString(), or "[object Name]".
bool object_as_text(Library &p_library, Object *p_object, std::string *r_text) {
	if (!p_object) {
		*r_text += "null";
		return true;
	}
	const ir::Class &klass = p_library.class_of(p_object);
	if (klass.element != Kind::Void) {
		return vector_text(p_library, static_cast<Vector *>(p_object), ",", r_text);
	}
	bool own = false;
	if (klass.table && (!table_text(p_library, p_object, r_text, &own) || own)) {
		return !p_library.runner.failing();
	}
	if (klass.to_string == NONE) {
		*r_text += object_text(p_library, p_object);
		return true;
	}
	Slot self;
	self.bits = 0;
	self.p = p_object;
	Slot result;
	result.bits = 0;
	if (!p_library.runner.run(klass.to_string, &self, 1, &result)) {
		return false; // Its error is on its way out of the code that asked.
	}
	Str *text = static_cast<Str *>(result.p);
	*r_text += text ? text->view() : std::string_view("null");
	if (release(text)) {
		free_string(text);
	}
	return true;
}

} // namespace

bool locale_text(Library &p_library, Kind p_kind, Slot p_value, std::string *r_text) {
	return element_locale_text(p_library, p_kind, p_value, r_text);
}

Slot present_or_null(const Library &p_library, Slot p_value) {
	return any::is_engine(p_value) && !p_library.heap.handles.present(any::pointer(p_value)) ? any::null() : p_value;
}

// A script object speaks for itself, which runs the program: the value is held while it does.
bool any_text(Library &p_library, Slot p_value, std::string *r_text) {
	if (const std::optional<std::string> plain = any::text(p_value)) {
		*r_text += *plain;
		return true;
	}
	if (any::tag(p_value) == AnyTag::Object) {
		Object *object = static_cast<Object *>(any::pointer(p_value));
		retain(object);
		const bool said = object_as_text(p_library, object, r_text);
		p_library.heap.release_object(object);
		return said;
	}
	return engine_text(p_library, any::pointer(p_value), r_text);
}

// By a handle of its own while the engine speaks, which can run a script. False if memory ran out, or if
// that script's toString() failed.
bool engine_text(Library &p_library, void *p_handle, std::string *r_text) {
	EngineHandles &handles = p_library.heap.handles;
	const uint32_t carried_before = p_library.runner.carries();
	void *held = p_handle ? handles.copy(p_handle) : nullptr;
	Str *text = held ? handles.text(held) : nullptr;
	*r_text += text ? text->view() : std::string_view("null");
	if (release(text)) {
		free_string(text);
	}
	if (held) {
		handles.drop(held);
		if (!text) {
			p_library.runner.out_of_memory();
			return false;
		}
	}
	// A script's toString() Godot ran for this text failed: Godot's own text is not the value's.
	return p_library.runner.carries() == carried_before;
}

// An element that is an object says what it is in its own words, which runs the program.
bool vector_text(Library &p_library, Vector *p_vector, std::string_view p_separator, std::string *r_text, bool p_locale) {
	if (p_library.runner.wording >= MAX_TEXT_DEPTH || stack_below(stack_floor())) {
		p_library.raise_overflow(); // One that holds itself ends here too: avmplus's join has no guard but the stack's.
		return false;
	}
	p_library.runner.wording++;
	const ir::Class &klass = p_library.class_of(p_vector);
	const Kind kind = klass.element;
	// An Array leaves null and undefined out (avmplus's Array.as, _join: `if (x != null)`, as ECMAScript 3
	// has it), and so does toLocaleString() of either (VectorImpl.as: `if (x !== undefined && x !== null)`);
	// a Vector's join writes them as String() does (VectorImpl.as, join: `s += this[y]`).
	const bool leaves_out = p_locale || (klass.array && kind == Kind::Any);
	bool done = true;
	for (uint32_t i = 0; done && i < p_vector->length; i++) {
		if (i) {
			append_text(r_text, p_separator); // Text is joined as Concat joins it: two halves that meet are one character.
		}
		const Slot element = p_vector->data[i];
		// A freed Godot object is null here too.
		const AnyTag tag = kind == Kind::Any ? any::tag(present_or_null(p_library, element)) : AnyTag::Undefined;
		const bool engine = kind == Kind::Ref || kind == Kind::Val;
		const bool nothing = kind == Kind::Any ? tag == AnyTag::Null || tag == AnyTag::Undefined
											   : ir::is_held(kind) && (!element.p || (engine && !p_library.heap.handles.present(element.p)));
		if (leaves_out && nothing) {
			continue;
		}
		done = p_locale ? locale_text(p_library, kind, element, r_text) : element_text(p_library, kind, element, r_text);
	}
	p_library.runner.wording--;
	return done;
}

// For `+` and `<`: an object or a Godot value becomes its text.
bool any_primitive(Library &p_library, Slot p_value, Slot *r_out, bool p_number) {
	const ir::Module &module = p_library.module();
	if (any::tag(p_value) != AnyTag::Object && !any::is_engine(p_value)) {
		*r_out = p_library.heap.share(Kind::Any, p_value);
		return true;
	}
	Object *object = held_object(p_value);
	if (p_number && object && object->klass == module.date_class) {
		*r_out = any_number(p_library, object->fields()[ir::DATE_TIME].d); // A Date's valueOf().
		return true;
	}
	// A class's own valueOf() comes first, if what it gives is not an object (ECMAScript's [[DefaultValue]]).
	const uint32_t value_of = object && module.classes[object->klass].element == Kind::Void ? find_method(p_library, object->klass, "valueOf") : NONE;
	if (value_of != NONE) {
		Slot self;
		self.bits = 0;
		self.p = object;
		Slot result;
		result.bits = 0;
		retain(object);
		const bool called = p_library.runner.run(value_of, &self, 1, &result);
		const Kind kind = module.functions[value_of].result;
		const Slot given = called && kind != Kind::Void ? to_any(p_library, kind, result) : any::undefined();
		if (called) {
			p_library.heap.drop(kind, result);
		}
		p_library.heap.release_object(object);
		if (!called) {
			return false;
		}
		if (any::tag(given) != AnyTag::Object && !any::is_engine(given)) {
			*r_out = given;
			return true;
		}
		p_library.heap.drop_any(given);
	}
	std::string words;
	if (!any_text(p_library, p_value, &words)) {
		return false;
	}
	*r_out = any::string(p_library.text(words));
	return true;
}

std::string date_text(const Library &p_library, const Object *p_date, ir::DateText p_which) {
	const double time = const_cast<Object *>(p_date)->fields()[ir::DATE_TIME].d;
	return date::text(p_which, time, std::isnan(time) ? 0 : p_library.heap.clock.local_offset(time));
}

// With no name (it could not be made, memory having run out), the class's.
std::string error_heading(const ir::Module &p_module, const Object *p_error) {
	Slot *fields = const_cast<Object *>(p_error)->fields();
	const Str *name = static_cast<const Str *>(fields[ir::ERROR_NAME].p);
	const Str *message = static_cast<const Str *>(fields[ir::ERROR_MESSAGE].p);
	std::string text = name ? std::string(name->view()) : p_module.classes[p_error->klass].name;
	if (message && message->size > 0) {
		text += ": " + std::string(message->view());
	}
	return text;
}

std::string class_name(const ir::Module &p_module, uint32_t p_code) {
	switch (p_code) {
		case ir::ANY_IS_INT:
			return "int";
		case ir::ANY_IS_UINT:
			return "uint";
		case ir::ANY_IS_NUMBER:
			return "Number";
		case ir::ANY_IS_STRING:
			return "String";
		case ir::ANY_IS_BOOLEAN:
			return "Boolean";
		case ir::ANY_IS_OBJECT:
			return "Object";
		default:
			break;
	}
	const std::string &name = p_code < p_module.classes.size() ? p_module.classes[p_code].name : std::string("?");
	const bool vector = p_code < p_module.classes.size() && p_module.classes[p_code].element != Kind::Void;
	return vector || name.rfind('.') == std::string::npos ? name : name.substr(name.rfind('.') + 1);
}

std::string object_text(const Library &p_library, Object *p_object) {
	if (!p_object) {
		return "null";
	}
	const ir::Module &module = p_library.module();
	const std::string &name = module.classes[p_object->klass].name;
	if (module.error_class != NONE && is_class(module, p_object, module.error_class)) {
		return error_heading(module, p_object); // A class of the program's that extends Error is named "Error".
	}
	if (p_object->klass == module.date_class) {
		return date_text(p_library, p_object, ir::DateText::Full);
	}
	if (p_object->klass == module.function_class) {
		return "function Function() {}"; // avmplus's Function.as: no source text is kept, for any function.
	}
	if (p_object->klass == module.class_class) {
		return "[class " + class_name(module, static_cast<uint32_t>(p_object->fields()[ir::CLASS_CODE].i)) + "]";
	}
	if (p_object->klass == module.regexp_class) {
		const Str *source = static_cast<const Str *>(p_object->fields()[ir::REGEXP_SOURCE].p);
		return regex::regexp_text(source ? source->view() : std::string_view(), static_cast<uint8_t>(p_object->fields()[ir::REGEXP_FLAGS].i));
	}
	return "[object " + name.substr(name.rfind('.') == std::string::npos ? 0 : name.rfind('.') + 1) + "]";
}

} // namespace vm
} // namespace kas3
