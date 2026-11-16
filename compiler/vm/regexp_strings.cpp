// String's match(), search() and replace() with a pattern, and split() by a RegExp: ECMAScript 3's
// 15.5.4.10 to 15.5.4.14.
#include "interpreter_internal.h"

#include "regex.h"
#include "text_units.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;

namespace {

Slot pointer_slot(void *p_value) {
	Slot slot;
	slot.bits = 0;
	slot.p = p_value;
	return slot;
}

std::string_view text_in(const void *p_text) {
	return p_text ? static_cast<const Str *>(p_text)->view() : std::string_view("null");
}

bool is_digit(char16_t p_unit) {
	return p_unit >= u'0' && p_unit <= u'9';
}

// The replacement with its $ codes (15.5.4.11): $$, $&, $`, $', and $1 to $99 for a group the pattern
// has (two digits if that is a group, else one; otherwise the $ is itself, as Node has it).
void expand(const std::u16string &p_with, const std::vector<int32_t> &p_captures, const std::u16string &p_text, std::u16string *r_out) {
	const auto group = [&](size_t p_group) {
		const int32_t start = p_captures[2 * p_group];
		if (start >= 0) {
			r_out->append(p_text, start, p_captures[2 * p_group + 1] - start);
		}
	};
	const size_t groups = p_captures.size() / 2 - 1;
	for (size_t at = 0; at < p_with.size(); at++) {
		const char16_t next = at + 1 < p_with.size() ? p_with[at + 1] : 0;
		const size_t one = is_digit(next) ? next - u'0' : 0;
		const size_t two = one * 10 + (at + 2 < p_with.size() && is_digit(p_with[at + 2]) ? p_with[at + 2] - u'0' : 0);
		if (p_with[at] != u'$' || at + 1 >= p_with.size()) {
			*r_out += p_with[at];
		} else if (next == u'$') {
			*r_out += u'$';
			at++;
		} else if (next == u'&') {
			group(0);
			at++;
		} else if (next == u'`') {
			r_out->append(p_text, 0, p_captures[0]);
			at++;
		} else if (next == u'\'') {
			r_out->append(p_text, p_captures[1], std::u16string::npos);
			at++;
		} else if (at + 2 < p_with.size() && is_digit(p_with[at + 2]) && two >= 1 && two <= groups) {
			group(two);
			at += 2;
		} else if (one >= 1 && one <= groups) {
			group(one);
			at++;
		} else {
			*r_out += u'$';
		}
	}
}

} // namespace

// The pattern of match() and search(): a RegExp, or what becomes one, its text (undefined is none).
std::shared_ptr<const regex::Program> Interpreter::pattern_argument(Slot p_pattern, Object **r_regexp, std::string *r_source) {
	*r_regexp = nullptr;
	if (is_regexp(p_pattern)) {
		*r_regexp = static_cast<Object *>(any::pointer(p_pattern));
		*r_source = text_in((*r_regexp)->fields()[ir::REGEXP_SOURCE].p);
		return pattern_of(*r_regexp);
	}
	if (any::tag(p_pattern) != AnyTag::Undefined) {
		const Slot held = share(Kind::Any, p_pattern);
		const bool said = any_text(held, r_source); // May run the program.
		drop_any(held);
		if (!said) {
			return nullptr;
		}
	}
	return pattern(*r_source, 0);
}

std::optional<std::vector<Interpreter::Found>> Interpreter::all_matches(const regex::Program &p_program, std::string_view p_source,
		const std::u16string &p_text) {
	std::vector<Found> matches;
	const bool global = (p_program.flags & regex::GLOBAL) != 0;
	for (size_t from = 0; from <= p_text.size() && (global || matches.empty());) {
		std::optional<Found> found = find_match(p_program, p_source, p_text, static_cast<uint32_t>(from));
		if (failing_) {
			return std::nullopt;
		}
		if (!found) {
			break;
		}
		const size_t end = static_cast<size_t>(found->captures[1]);
		from = end == from ? end + 1 : end; // A match of nothing: the next is looked for a unit on.
		matches.push_back(std::move(*found));
	}
	return matches;
}

// match(pattern): as exec() if the pattern is not global; else every match, as text, and lastIndex 0.
void Interpreter::string_match(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	Object *regexp = nullptr;
	std::string source;
	const std::shared_ptr<const regex::Program> program = pattern_argument(stack_[frames_.back().base + list[1]], &regexp, &source);
	if (!program) {
		return;
	}
	const std::u16string units = units_of(text_in(stack_[frames_.back().base + list[0]].p));
	const std::optional<std::vector<Found>> matches = all_matches(*program, source, units);
	if (!matches) {
		return;
	}
	const bool global = (program->flags & regex::GLOBAL) != 0;
	if (regexp && global) { // One that is not global is left as it is, as its exec() leaves it.
		regexp->fields()[ir::REGEXP_LAST_INDEX].d = 0;
	}
	Vector *array = nullptr;
	if (!global && !matches->empty()) {
		array = match_array(matches->front(), units, *program);
	} else if (!matches->empty()) {
		array = fresh_vector(module_.array_class, matches->size());
		for (size_t i = 0; array && i < matches->size(); i++) {
			const std::vector<int32_t> &at = (*matches)[i].captures;
			Str *piece = text(text_of_units(std::u16string_view(units).substr(at[0], at[1] - at[0])));
			if (!piece) {
				release_object(array);
				return;
			}
			array->data[i] = any::string(piece);
		}
	}
	if (!matches->empty() && !array) {
		return;
	}
	put(Kind::Obj, stack_[frames_.back().base + p_in.a], pointer_slot(array));
}

// search(pattern): where the first match starts, or -1. lastIndex and global are not looked at.
void Interpreter::string_search(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	Object *regexp = nullptr;
	std::string source;
	const std::shared_ptr<const regex::Program> program = pattern_argument(stack_[frames_.back().base + list[1]], &regexp, &source);
	if (!program) {
		return;
	}
	const std::u16string units = units_of(text_in(stack_[frames_.back().base + list[0]].p));
	const std::optional<Found> found = find_match(*program, source, units, 0);
	if (!failing_) {
		set_int32(stack_[frames_.back().base + p_in.a], found ? found->captures[0] : -1);
	}
}

// What one match is replaced by: the function's result as text, called with the match, its groups, where it
// starts and the whole text; or the replacement, its $ codes expanded for a RegExp. False if the function
// raised.
bool Interpreter::replacement(Object *p_function, const std::u16string &p_with, bool p_expand, const Found &p_found, const std::u16string &p_text,
		std::u16string *r_out) {
	if (!p_function) {
		if (p_expand) {
			expand(p_with, p_found.captures, p_text, r_out);
		} else {
			*r_out += p_with;
		}
		return true;
	}
	std::vector<Slot> arguments;
	const auto add_text = [&](std::u16string_view p_units) {
		Str *made = text(text_of_units(p_units));
		arguments.push_back(made ? any::string(made) : any::undefined());
		return made != nullptr;
	};
	bool made = true;
	for (size_t i = 0; made && i + 1 < p_found.captures.size(); i += 2) {
		const int32_t start = p_found.captures[i];
		if (start < 0) {
			arguments.push_back(any::undefined());
		} else {
			made = add_text(std::u16string_view(p_text).substr(start, p_found.captures[i + 1] - start));
		}
	}
	arguments.push_back(any::integer(p_found.captures[0]));
	made = made && add_text(p_text);
	const std::optional<Slot> result = made ? call_function_within(p_function, arguments.data(), arguments.size()) : std::nullopt;
	for (const Slot argument : arguments) {
		drop_any(argument);
	}
	if (!result) {
		return false;
	}
	std::string said;
	const bool spoke = any_text(*result, &said); // May run the program too.
	drop_any(*result);
	*r_out += units_of(said);
	return spoke;
}

// What replace() replaces: every match of a pattern as match() finds them, or the first place of a text.
// Nothing if the pattern's matcher raised.
std::optional<std::vector<Interpreter::Found>> Interpreter::places_to_replace(const regex::Program *p_program, const std::string &p_needle,
		const std::u16string &p_units) {
	if (p_program) {
		return all_matches(*p_program, p_needle, p_units);
	}
	std::vector<Found> matches;
	if (const size_t at = p_units.find(units_of(p_needle)); at != std::u16string::npos) {
		matches.push_back(Found{ { static_cast<int32_t>(at), static_cast<int32_t>(at + units_of(p_needle).size()) } });
	}
	return matches;
}

// replace(pattern, replacement). A pattern that is not a RegExp is text, and only its first place is
// replaced, with the replacement as it is written (the ruling string_methods.cpp follows for two Strings;
// Flash's by recollection). Every match is found before a replacement function is first called.
void Interpreter::string_replace(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const auto argument = [&](size_t p_index) { return stack_[frames_.back().base + list[p_index]]; };
	// The RegExp is held from here on: the replacement's text, or the function, may let go of it.
	Object *regexp = is_regexp(argument(1)) ? static_cast<Object *>(any::pointer(argument(1))) : nullptr;
	retain(regexp);
	std::string needle, with;
	std::shared_ptr<const regex::Program> program;
	if (regexp) {
		program = pattern_of(regexp);
		needle = text_in(regexp->fields()[ir::REGEXP_SOURCE].p);
	} else {
		const Slot held = share(Kind::Any, argument(1));
		const bool said = any_text(held, &needle);
		drop_any(held);
		if (!said) {
			return;
		}
	}
	const Slot given = argument(2);
	const Object *object = any::tag(given) == AnyTag::Object ? static_cast<const Object *>(any::pointer(given)) : nullptr;
	Object *function = object && object->klass == module_.function_class ? const_cast<Object *>(object) : nullptr;
	bool said = !regexp || program;
	if (said && !function) {
		const Slot held = share(Kind::Any, given);
		said = any_text(held, &with);
		drop_any(held);
	}
	if (!said) {
		release_object(regexp);
		return;
	}
	const std::u16string units = units_of(text_in(argument(0).p));
	const std::optional<std::vector<Found>> found = places_to_replace(regexp ? program.get() : nullptr, needle, units);
	if (!found) {
		release_object(regexp);
		return;
	}
	const std::vector<Found> &matches = *found;
	retain(function); // Held while it runs: it may let go of itself.
	const std::u16string with_units = units_of(with);
	std::u16string out;
	size_t previous = 0;
	bool done = true;
	for (size_t i = 0; done && i < matches.size(); i++) {
		out.append(units, previous, matches[i].captures[0] - previous);
		done = replacement(function, with_units, regexp != nullptr, matches[i], units, &out);
		previous = static_cast<size_t>(matches[i].captures[1]);
	}
	if (done && regexp && (program->flags & regex::GLOBAL)) {
		regexp->fields()[ir::REGEXP_LAST_INDEX].d = 0;
	}
	release_object(function);
	release_object(regexp);
	if (!done) {
		return;
	}
	out.append(units, previous, std::u16string::npos);
	assign(Kind::Str, stack_[frames_.back().base + p_in.a], text(text_of_units(out)));
}

// split() by a RegExp (15.5.4.14): the text between matches, each followed by the groups of the match
// after it; a match of nothing where the last one ended does not split. At most p_most pieces.
Vector *Interpreter::split_by_pattern(Object *p_regexp, std::string_view p_text, uint32_t p_most) {
	const std::shared_ptr<const regex::Program> program = pattern_of(p_regexp);
	if (!program) {
		return nullptr;
	}
	const std::string_view source = text_in(p_regexp->fields()[ir::REGEXP_SOURCE].p);
	const std::u16string units = units_of(p_text);
	std::vector<std::optional<std::u16string_view>> pieces;
	const std::u16string_view all(units);
	size_t piece_start = 0;
	for (size_t from = 0; pieces.size() < p_most && (from < units.size() || (units.empty() && from == 0));) {
		const std::optional<Found> found = find_match(*program, source, units, static_cast<uint32_t>(from));
		if (failing_) {
			return nullptr;
		}
		if (units.empty()) {
			if (!found) {
				pieces.emplace_back(all); // An empty text that the pattern does not match is one empty piece.
			}
			piece_start = 1;
			break;
		}
		if (!found || static_cast<size_t>(found->captures[0]) >= units.size()) {
			break;
		}
		const size_t start = static_cast<size_t>(found->captures[0]), end = static_cast<size_t>(found->captures[1]);
		if (end == piece_start) {
			from = start + 1;
			continue;
		}
		pieces.emplace_back(all.substr(piece_start, start - piece_start));
		for (size_t group = 2; group + 1 < found->captures.size() && pieces.size() < p_most; group += 2) {
			const int32_t at = found->captures[group];
			pieces.push_back(at < 0 ? std::nullopt : std::optional<std::u16string_view>(all.substr(at, found->captures[group + 1] - at)));
		}
		from = piece_start = end;
	}
	if (pieces.size() < p_most && piece_start <= units.size()) {
		pieces.emplace_back(all.substr(piece_start));
	}
	Vector *made = fresh_vector(module_.array_class, pieces.size());
	for (size_t i = 0; made && i < pieces.size(); i++) {
		Str *piece = pieces[i] ? text(text_of_units(*pieces[i])) : nullptr;
		if (pieces[i] && !piece) {
			release_object(made);
			return nullptr;
		}
		made->data[i] = piece ? any::string(piece) : any::undefined();
	}
	return made;
}

} // namespace vm
} // namespace kas3
