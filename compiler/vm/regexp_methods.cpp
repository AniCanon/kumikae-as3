#include "interpreter_internal.h"
#include "table.h"

#include "regex.h"
#include "text_units.h"


namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::RegExpFunction;

namespace {

// The patterns read are kept up to this many; then the cache starts again.
constexpr size_t MOST_PATTERNS = 256;

Slot pointer_slot(void *p_value) {
	Slot slot;
	slot.bits = 0;
	slot.p = p_value;
	return slot;
}

std::string_view text_in(const Str *p_text) {
	return p_text ? p_text->view() : std::string_view("null");
}

uint8_t bit_of(RegExpFunction p_property) {
	switch (p_property) {
		case RegExpFunction::Global:
			return regex::GLOBAL;
		case RegExpFunction::IgnoreCase:
			return regex::IGNORE_CASE;
		case RegExpFunction::Multiline:
			return regex::MULTILINE;
		case RegExpFunction::Dotall:
			return regex::DOTALL;
		default:
			return regex::EXTENDED;
	}
}

} // namespace

std::shared_ptr<const regex::Program> Interpreter::pattern(std::string_view p_source, uint8_t p_flags) {
	std::string key(1, static_cast<char>(p_flags));
	key += p_source;
	if (const auto found = patterns_.find(key); found != patterns_.end()) {
		return found->second;
	}
	std::string error;
	std::optional<regex::Program> program = regex::compile(units_of(p_source), p_flags, &error);
	if (!program) {
		const uint32_t klass = module_.syntax_error_class != ir::NONE ? module_.syntax_error_class : module_.error_class;
		raise(klass, "the regular expression " + regex::regexp_text(p_source, p_flags) + " can't be read: " + error + ".");
		return nullptr;
	}
	if (patterns_.size() >= MOST_PATTERNS) {
		patterns_.clear();
	}
	auto made = std::make_shared<const regex::Program>(std::move(*program));
	patterns_.emplace(std::move(key), made);
	return made;
}

std::shared_ptr<const regex::Program> Interpreter::pattern_of(const Object *p_regexp) {
	Object *regexp = const_cast<Object *>(p_regexp);
	return pattern(text_in(static_cast<const Str *>(regexp->fields()[ir::REGEXP_SOURCE].p)), static_cast<uint8_t>(regexp->fields()[ir::REGEXP_FLAGS].i));
}

bool Interpreter::is_regexp(Slot p_any) const {
	const Object *object = any::tag(p_any) == AnyTag::Object ? static_cast<const Object *>(any::pointer(p_any)) : nullptr;
	return object && module_.regexp_class != ir::NONE && object->klass == module_.regexp_class;
}

std::optional<Interpreter::Found> Interpreter::find_match(const regex::Program &p_program, std::string_view p_source, const std::u16string &p_text,
		uint32_t p_from) {
	Found found;
	switch (regex::search(p_program, p_text, p_from, &found.captures, regex::limits_for(p_text.size()))) {
		case regex::Outcome::Found:
			return found;
		case regex::Outcome::NotFound:
			break;
		case regex::Outcome::Limit:
			raise(module_.error_class, "the regular expression " + regex::regexp_text(p_source, p_program.flags) + " took too many steps, and was stopped.");
			break;
	}
	return std::nullopt;
}

// What exec() gives: the match and each group, as Strings, undefined for a group that took no part; and as
// its properties `index` (where the match starts), `input` (the text searched) and each named group.
Vector *Interpreter::match_array(const Found &p_found, const std::u16string &p_text, const regex::Program &p_program, Str *p_input) {
	const size_t count = p_found.captures.size() / 2;
	Vector *array = fresh_vector(module_.array_class, count);
	for (size_t i = 0; array && i < count; i++) {
		const int32_t start = p_found.captures[2 * i], end = p_found.captures[2 * i + 1];
		if (start < 0) {
			continue; // Zero: undefined.
		}
		Str *piece = text(text_of_units(std::u16string_view(p_text).substr(start, end - start)));
		if (!piece) {
			release_object(array);
			return nullptr;
		}
		array->data[i] = any::string(piece);
	}
	if (!array) {
		return nullptr;
	}
	const auto property = [&](const std::string &p_name, Slot p_value) {
		PropertyKey key;
		key.text = p_name;
		const bool set = set_entry(properties_of(array), key, p_value);
		drop_any(p_value);
		return set;
	};
	// The text searched itself, shared: a loop of exec() makes no copy of it for each match.
	Str *input = p_input ? p_input : null_text_;
	retain(input);
	// `input` first: the property takes the string and lets it go, whatever comes after.
	bool ok = property("input", any::string(input)) && property("index", any::integer(p_found.captures[0]));
	for (size_t i = 0; ok && i < p_program.names.size(); i++) {
		if (!p_program.names[i].empty() && i + 1 < count) {
			ok = property(text_of_units(p_program.names[i]), share(Kind::Any, array->data[i + 1]));
		}
	}
	if (!ok) {
		release_object(array);
		return nullptr;
	}
	return array;
}

void Interpreter::regexp_call(const Function &p_function, const Instruction &p_in) {
	const RegExpFunction which = static_cast<RegExpFunction>(p_in.b);
	switch (which) {
		case RegExpFunction::New:
			regexp_new(p_function, p_in);
			return;
		case RegExpFunction::Exec:
		case RegExpFunction::Test:
			regexp_exec(p_function, p_in);
			return;
		case RegExpFunction::Match:
			string_match(p_function, p_in);
			return;
		case RegExpFunction::Search:
			string_search(p_function, p_in);
			return;
		case RegExpFunction::Replace:
			string_replace(p_function, p_in);
			return;
		default:
			break;
	}
	Slot *r = stack_.data() + frames_.back().base;
	Object *regexp = static_cast<Object *>(r[p_function.argument_lists[p_in.c][0]].p);
	if (!regexp) {
		raise_null();
		return;
	}
	const Slot read = regexp_read(regexp, which);
	if (which == RegExpFunction::Source) {
		assign(Kind::Str, r[p_in.a], read.p);
	} else {
		r[p_in.a] = read;
	}
}

Slot Interpreter::regexp_read(Object *p_regexp, RegExpFunction p_property) {
	Slot out;
	out.bits = 0;
	if (p_property == RegExpFunction::Source) {
		out.p = p_regexp->fields()[ir::REGEXP_SOURCE].p;
		retain(static_cast<Str *>(out.p));
	} else {
		out.b = (p_regexp->fields()[ir::REGEXP_FLAGS].i & bit_of(p_property)) != 0;
	}
	return out;
}

// new RegExp(source, flags): each as its text, undefined as none; a RegExp as the source is copied, and
// may not be given flags (TypeError #1100). The pattern is read here, so that one that can't be read is
// an error where it is made.
void Interpreter::regexp_new(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const size_t base = frames_.back().base;
	const auto words = [&](size_t p_index, std::string *r_text) {
		const Slot value = stack_[base + list[p_index]];
		if (p_function.registers[list[p_index]] == Kind::Str) {
			*r_text = text_in(static_cast<const Str *>(value.p));
			return true;
		}
		if (any::tag(value) == AnyTag::Undefined) {
			return true;
		}
		const Slot held = share(Kind::Any, value);
		const bool said = any_text(held, r_text); // May run the program, and move the stack.
		drop_any(held);
		return said;
	};
	std::string source, flags_text;
	uint8_t flags = 0;
	const Slot first = stack_[base + list[0]];
	if (p_function.registers[list[0]] == Kind::Any && is_regexp(first)) {
		if (any::tag(stack_[base + list[1]]) != AnyTag::Undefined) {
			raise(module_.type_error_class, "Error #1100: Cannot supply flags when constructing one RegExp from another.");
			return;
		}
		Object *from = static_cast<Object *>(any::pointer(first));
		source = text_in(static_cast<const Str *>(from->fields()[ir::REGEXP_SOURCE].p));
		flags = static_cast<uint8_t>(from->fields()[ir::REGEXP_FLAGS].i);
	} else if (!words(0, &source) || !words(1, &flags_text)) {
		return;
	} else {
		flags = regex::read_flags(flags_text);
	}
	if (!pattern(source, flags)) {
		return;
	}
	Object *made = new_object(module_.regexp_class, module_.classes[module_.regexp_class].fields.size(), collector_.list_for(module_.regexp_class));
	Str *kept = made ? text(source) : nullptr;
	if (!kept) {
		if (made) {
			free_object(made);
		}
		out_of_memory();
		return;
	}
	made->fields()[ir::REGEXP_SOURCE].p = kept;
	made->fields()[ir::REGEXP_FLAGS].i = flags;
	put(Kind::Obj, stack_[frames_.back().base + p_in.a], pointer_slot(made));
}

// exec(text) and test(text), as ECMAScript 3 has them (15.10.6.2): a global RegExp starts from lastIndex
// and moves it past the match, or to 0 when the search fails. One that is not global neither reads nor
// writes it, as avmplus has it (ECMAScript 3 has a failed search set it to 0 either way).
void Interpreter::regexp_exec(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	Slot *r = stack_.data() + frames_.back().base;
	Object *regexp = static_cast<Object *>(r[list[0]].p);
	if (!regexp) {
		raise_null();
		return;
	}
	const bool test = static_cast<RegExpFunction>(p_in.b) == RegExpFunction::Test;
	Str *subject = r[list[1]].p ? static_cast<Str *>(r[list[1]].p) : null_text_;
	Slot out;
	if (regexp_run(regexp, subject, test, &out)) {
		if (test) {
			r[p_in.a] = out;
		} else {
			put(Kind::Any, r[p_in.a], out);
		}
	}
}

std::shared_ptr<const std::u16string> Interpreter::subject_units(Str *p_text) {
	if (p_text != subject_ || !subject_units_) {
		forget_subject();
		subject_units_ = std::make_shared<const std::u16string>(units_of(p_text->view()));
		retain(p_text);
		subject_ = p_text;
	}
	return subject_units_;
}

void Interpreter::forget_subject() {
	subject_units_.reset();
	if (release(subject_)) {
		free_string(subject_);
	}
	subject_ = nullptr;
}

bool Interpreter::regexp_run(Object *p_regexp, Str *p_text, bool p_test, Slot *r_out) {
	const std::shared_ptr<const regex::Program> program = pattern_of(p_regexp);
	if (!program) {
		return false;
	}
	const std::string_view source = text_in(static_cast<const Str *>(p_regexp->fields()[ir::REGEXP_SOURCE].p));
	// Held here: what this run makes can't run the program, but nothing is left to chance.
	const std::shared_ptr<const std::u16string> held = subject_units(p_text);
	const std::u16string &units = *held;
	const bool global = (program->flags & regex::GLOBAL) != 0;
	Slot &last = p_regexp->fields()[ir::REGEXP_LAST_INDEX];
	const int32_t from = global ? last.i : 0;
	std::optional<Found> found;
	if (from >= 0 && static_cast<uint32_t>(from) <= units.size()) {
		found = find_match(*program, source, units, static_cast<uint32_t>(from));
	}
	if (failing_) {
		return false;
	}
	if (global) {
		set_int32(last, found ? found->captures[1] : 0);
	}
	r_out->bits = 0;
	if (p_test) {
		r_out->b = found.has_value();
		return true;
	}
	Vector *array = found ? match_array(*found, units, *program, p_text) : nullptr;
	if (found && !array) {
		return false;
	}
	*r_out = array ? any::object(array) : any::null();
	return true;
}

} // namespace vm
} // namespace kas3
