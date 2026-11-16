// RegExp's and Date's members by name, on a value held untyped (`u.lastIndex`, `u.time = 0`, `u.exec(s)`):
// what the lowerer makes of the same names on a typed value (ir/library_names.h), done as the program
// runs. A method read as a value, not called, can't be made yet, and says so.
#include "interpreter_internal.h"

#include "library_names.h"

#include <array>

namespace kas3 {
namespace vm {

using ir::Kind;

namespace {

bool of_class(const Object *p_object, uint32_t p_class) {
	return p_class != ir::NONE && p_object->klass == p_class;
}

} // namespace

std::optional<bool> Interpreter::library_get(Object *p_object, std::string_view p_name, Slot *r_value) {
	if (of_class(p_object, module_.regexp_class)) {
		if (p_name == "lastIndex") {
			*r_value = to_any(Kind::F64, p_object->fields()[ir::REGEXP_LAST_INDEX]);
			return true;
		}
		if (const ir::RegExpProperty *property = ir::regexp_property(p_name)) {
			Slot read = regexp_read(p_object, property->function);
			*r_value = to_any(property->kind, read);
			drop(property->kind, read);
			return true;
		}
	} else if (of_class(p_object, module_.date_class)) {
		if (const std::optional<ir::DateAccess> access = ir::date_property(p_name)) {
			Slot read;
			date_apply(p_object, access->read, nullptr, 0, &read);
			*r_value = to_any(Kind::F64, read);
			return true;
		}
	} else {
		return std::nullopt;
	}
	// A method read and not called, or a property the class does not declare (AS3's RegExp and Date are
	// dynamic, and may have more).
	const std::string &klass = module_.classes[p_object->klass].name;
	const bool method = of_class(p_object, module_.regexp_class) ? ir::regexp_method(p_name).has_value() : ir::date_method(p_name).has_value();
	raise_unsupported(method ? "reading the method '" + std::string(p_name) + "' of a " + klass + " without calling it"
							 : "reading '" + std::string(p_name) + "', which a " + klass + " does not declare, by its name");
	return false;
}

std::optional<bool> Interpreter::library_set(Object *p_object, std::string_view p_name, Slot p_value) {
	const bool regexp = of_class(p_object, module_.regexp_class);
	if (!regexp && !of_class(p_object, module_.date_class)) {
		return std::nullopt;
	}
	const Slot holder = any::object(p_object);
	const std::optional<ir::DateAccess> access = regexp ? std::nullopt : ir::date_property(p_name);
	if (regexp ? p_name != "lastIndex" && ir::regexp_property(p_name) : access && access->write == ir::NONE) {
		return refuse_write("1074", "Illegal write to read-only property ", p_name, holder);
	}
	if (regexp ? ir::regexp_method(p_name).has_value() : ir::date_method(p_name).has_value()) {
		return refuse_write("1037", "Cannot assign to a method ", p_name, holder);
	}
	if (regexp ? p_name != "lastIndex" : !access) {
		raise_unsupported("adding a property to a " + module_.classes[p_object->klass].name);
		return false;
	}
	const uint32_t write = regexp ? ir::NONE : access->write;
	Slot number;
	if (!from_any(p_value, Kind::F64, ir::NONE, &number)) {
		return false;
	}
	if (write == ir::NONE) {
		p_object->fields()[ir::REGEXP_LAST_INDEX].d = number.d;
		return true;
	}
	Slot ignored;
	return date_apply(p_object, write, &number.d, 1, &ignored);
}

std::optional<bool> Interpreter::library_call(Object *p_object, std::string_view p_name, const std::vector<Slot> &p_given, uint32_t p_out) {
	Slot out;
	Kind kind = Kind::Any;
	if (of_class(p_object, module_.regexp_class)) {
		const std::optional<ir::RegExpFunction> which = ir::regexp_method(p_name);
		if (!which) {
			return std::nullopt;
		}
		// The text, as exec(s:String = "") takes it: undefined is null, and null is "null".
		Slot text;
		text.bits = 0;
		if (!p_given.empty() && !from_any(p_given[0], Kind::Str, ir::NONE, &text)) {
			return false;
		}
		if (p_given.empty()) {
			text.p = this->text(std::string());
		}
		Str *given = static_cast<Str *>(text.p);
		const bool test = *which == ir::RegExpFunction::Test;
		const bool ran = (given || !p_given.empty()) && regexp_run(p_object, given ? given : null_text_, test, &out);
		drop(Kind::Str, text);
		if (!ran) {
			return false;
		}
		kind = test ? Kind::Bool : Kind::Any;
	} else if (of_class(p_object, module_.date_class)) {
		const std::optional<uint32_t> code = ir::date_method(p_name);
		if (!code) {
			return std::nullopt;
		}
		// The parts a setter writes, as Numbers; a setter takes four at most.
		std::array<double, 4> parts{};
		const size_t count = std::min(p_given.size(), parts.size());
		for (size_t i = 0; i < count; i++) {
			Slot number;
			if (!from_any(p_given[i], Kind::F64, ir::NONE, &number)) {
				return false;
			}
			parts[i] = number.d;
		}
		if (!date_apply(p_object, *code, parts.data(), count, &out)) {
			return false;
		}
		kind = static_cast<ir::DateFunction>(*code & 0xFF) == ir::DateFunction::Text ? Kind::Str : Kind::F64;
	} else {
		return std::nullopt;
	}
	if (p_out != ir::NONE) {
		put(Kind::Any, stack_[frames_.back().base + p_out], to_any(kind, out));
	}
	drop(kind, out);
	return true;
}

bool Interpreter::library_has(const Object *p_object, std::string_view p_name) const {
	if (of_class(p_object, module_.regexp_class)) {
		return p_name == "lastIndex" || ir::regexp_property(p_name) || ir::regexp_method(p_name);
	}
	return of_class(p_object, module_.date_class) && (ir::date_property(p_name) || ir::date_method(p_name));
}

} // namespace vm
} // namespace kas3
