#include "lower_dynamic.h"
#include "lower_library.h"

namespace kas3 {
namespace ir {

bool Library::is_json(const Symbol *p_owner) {
	return p_owner && p_owner->kind == SymbolKind::Class && p_owner->name == "JSON" && p_owner->unit && p_owner->unit->is_builtin &&
			(!p_owner->package || p_owner->package->name.empty());
}

// JSON.parse(text, reviver) and JSON.stringify(value, replacer, space), and a Dictionary's toJSON(key).
// Each argument is converted as its parameter is declared; one left out is null.
uint32_t Library::json_call() {
	const std::string_view name = function_->name;
	const bool to_json = is_dictionary(function_->owner);
	if (!decl_ || (to_json ? name != "toJSON" : name != "parse" && name != "stringify")) {
		return refused("", Kind::Any);
	}
	// What the instruction makes, and what it may raise or call.
	Dynamic(l_).table_class(false);
	if (l_.array_) {
		l_.vector_class(l_.compilation_.instance_type(l_.array_));
	}
	if (Symbol *syntax_error = l_.compilation_.builtin("SyntaxError")) {
		l_.class_index(syntax_error, &l_.module_.syntax_error_class);
	}
	uint32_t ignored = NONE;
	l_.class_index(l_.function_, &ignored);
	l_.use_argument_error();

	std::vector<uint32_t> list;
	if (to_json) {
		list.push_back(l_.settled(l_.receiver(object()), later(0), position_));
	}
	for (uint32_t at = 0; at < decl_->parameters.count; at++) {
		const Type *type = l_.compilation_.type_of(decl_->parameters[at].type);
		Kind kind = Kind::Any;
		l_.kind_of(type, &kind);
		if (at < call_->arguments.count) {
			list.push_back(l_.settled(l_.typed(call_->arguments[at], type, kind), later(at + 1), position_));
			continue;
		}
		const uint32_t missing = l_.reg(kind);
		if (kind == Kind::Any) {
			l_.emit(Op::AnyConst, position_, missing, 1);
		} else {
			l_.emit(Op::ConstNull, position_, missing);
		}
		list.push_back(missing);
	}
	if (to_json) {
		l_.emit(Op::NullCheck, position_, list[0]);
	}
	const JsonFunction which = to_json ? JsonFunction::ToJson : name == "parse" ? JsonFunction::Parse : JsonFunction::Stringify;
	return emit(Op::JsonCall, static_cast<uint32_t>(which), std::move(list), which == JsonFunction::Stringify ? Kind::Str : Kind::Any);
}

} // namespace ir
} // namespace kas3
