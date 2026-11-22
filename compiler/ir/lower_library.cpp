#include "lower_dynamic.h"
#include "lower_library.h"

#include <array>

namespace kas3 {
namespace ir {

namespace {

struct StringMethod {
	std::string_view name;
	StringFunction function;
	Kind result;
};

constexpr std::array<StringMethod, 23> STRING_METHODS = { {
		{ "charAt", StringFunction::CharAt, Kind::Str },
		{ "charCodeAt", StringFunction::CharCodeAt, Kind::F64 },
		{ "indexOf", StringFunction::IndexOf, Kind::I32 },
		{ "lastIndexOf", StringFunction::LastIndexOf, Kind::I32 },
		{ "substr", StringFunction::Substr, Kind::Str },
		{ "substring", StringFunction::Substring, Kind::Str },
		{ "slice", StringFunction::Slice, Kind::Str },
		{ "concat", StringFunction::Concat, Kind::Str },
		{ "toUpperCase", StringFunction::Upper, Kind::Str },
		{ "toLocaleUpperCase", StringFunction::Upper, Kind::Str },
		{ "toLowerCase", StringFunction::Lower, Kind::Str },
		{ "toLocaleLowerCase", StringFunction::Lower, Kind::Str },
		{ "replace", StringFunction::Replace, Kind::Str },
		{ "localeCompare", StringFunction::LocaleCompare, Kind::I32 },
		{ "fromCharCode", StringFunction::FromCharCode, Kind::Str },
		{ "parseInt", StringFunction::ParseInt, Kind::F64 },
		{ "parseFloat", StringFunction::ParseFloat, Kind::F64 },
		{ "escape", StringFunction::Escape, Kind::Str },
		{ "unescape", StringFunction::Unescape, Kind::Str },
		{ "encodeURI", StringFunction::EncodeURI, Kind::Str },
		{ "encodeURIComponent", StringFunction::EncodeURIComponent, Kind::Str },
		{ "decodeURI", StringFunction::DecodeURI, Kind::Str },
		{ "decodeURIComponent", StringFunction::DecodeURIComponent, Kind::Str },
} };

struct NumberMethod {
	std::string_view name;
	NumberFormat format;
};

constexpr std::array<NumberMethod, 4> NUMBER_METHODS = { {
		{ "toString", NumberFormat::Radix },
		{ "toFixed", NumberFormat::Fixed },
		{ "toExponential", NumberFormat::Exponential },
		{ "toPrecision", NumberFormat::Precision },
} };

template <class Entry, size_t Count>
const Entry *find(const std::array<Entry, Count> &p_table, std::string_view p_name) {
	for (const Entry &entry : p_table) {
		if (entry.name == p_name) {
			return &entry;
		}
	}
	return nullptr;
}

} // namespace

Library::Library(Lowerer &p_lowerer, Call *p_call, Symbol *p_function) :
		l_(p_lowerer), call_(p_call), function_(p_function), position_(p_call->offset) {
	const bool declared = p_function && p_function->node && p_function->node->kind == NodeKind::FunctionDecl;
	decl_ = declared ? static_cast<FunctionDecl *>(p_function->node) : nullptr;
}

Expr *Library::object() const {
	return call_->callee->kind == NodeKind::Member ? static_cast<Member *>(call_->callee)->object : nullptr;
}

uint32_t Library::refused(std::string_view p_why, Kind p_kind) {
	l_.refuse(call_->callee->offset, "calling '" + std::string(function_ ? function_->name : "toString") + "'" + std::string(p_why));
	return l_.default_value(p_kind, position_);
}

Expr *Library::later(uint32_t p_from) const {
	return Lowerer::writer(call_->arguments, p_from);
}

uint32_t Library::left_out(const Parameter &p_parameter, Kind p_kind) {
	const Expr *written = p_parameter.default_value;
	uint32_t out = l_.reg(p_kind);
	if (written && written->kind == NodeKind::StringLit) {
		l_.emit(Op::ConstStr, position_, out, l_.string(Lowerer::unescape(static_cast<const StringLit *>(written)->text)));
	} else if (written && written->kind == NodeKind::NumberLit) {
		const double value = Lowerer::literal_value(static_cast<const NumberLit *>(written));
		if (p_kind == Kind::F64) {
			l_.emit(Op::ConstF64, position_, out, l_.number(value));
		} else {
			l_.emit(p_kind == Kind::U32 ? Op::ConstU32 : Op::ConstI32, position_, out, static_cast<uint32_t>(static_cast<int64_t>(value)));
		}
	} else if (written && written->kind == NodeKind::Name && static_cast<const Name *>(written)->text == "undefined" && p_kind == Kind::F64) {
		l_.emit(Op::ConstF64, position_, out, l_.number(std::nan("")));
	} else {
		l_.refuse(call_->callee->offset, "leaving out an argument of '" + std::string(function_->name) + "'");
	}
	return out;
}

std::vector<uint32_t> Library::arguments(uint32_t p_receiver, Kind p_rest) {
	std::vector<uint32_t> list;
	if (p_receiver != NONE) {
		list.push_back(l_.settled(p_receiver, later(0), position_));
	}
	const uint32_t count = decl_ ? decl_->parameters.count : 0;
	uint32_t at = 0;
	for (; at < count && !decl_->parameters[at].is_rest; at++) {
		const Parameter &parameter = decl_->parameters[at];
		const Type *type = l_.compilation_.type_of(parameter.type);
		Kind kind = Kind::F64;
		l_.kind_of(type, &kind);
		kind = kind == Kind::Any ? Kind::Str : kind; // replace(): the checks before let only a String through.
		list.push_back(at < call_->arguments.count ? l_.settled(l_.typed(call_->arguments[at], type, kind), later(at + 1), position_)
												   : left_out(parameter, kind));
	}
	for (; p_rest != Kind::Void && at < call_->arguments.count; at++) {
		Expr *argument = call_->arguments[at];
		const uint32_t value = p_rest == Kind::Str && l_.untyped(argument) ? l_.any_text(argument) : l_.expression(argument, p_rest);
		list.push_back(l_.settled(value, later(at + 1), position_));
	}
	return list;
}

uint32_t Library::emit(Op p_op, uint32_t p_which, std::vector<uint32_t> p_list, Kind p_result) {
	const uint32_t out = l_.reg(p_result);
	l_.emit(p_op, position_, out, p_which, l_.arguments(std::move(p_list)));
	return out;
}

uint32_t Library::string_method() {
	const StringMethod *method = find(STRING_METHODS, function_->name);
	const std::string_view name = function_->name;
	if (name == "toString" || name == "valueOf") {
		const uint32_t text = l_.expression(object(), Kind::Str);
		l_.emit(Op::NullCheck, position_, text);
		return text;
	}
	if (name == "split") {
		return split();
	}
	if (name == "match" || name == "search") {
		return pattern_call(name == "match" ? RegExpFunction::Match : RegExpFunction::Search);
	}
	if (!method) {
		return refused("", Kind::Str);
	}
	if (method->function == StringFunction::Replace) {
		// Two strings, known to be strings: one instruction that runs nothing. Anything else is found out
		// as the call runs (a RegExp, a function).
		bool strings = call_->arguments.count == 2;
		for (Expr *argument : call_->arguments) {
			strings = strings && l_.is_string_type(argument->type);
		}
		if (!strings) {
			return pattern_call(RegExpFunction::Replace);
		}
	}
	const uint32_t text = l_.expression(object(), Kind::Str);
	std::vector<uint32_t> list = arguments(text, Kind::Str);
	l_.emit(Op::NullCheck, position_, list[0]);
	return emit(Op::StrCall, static_cast<uint32_t>(method->function), std::move(list), method->result);
}

uint32_t Library::split() {
	Expr *delimiter = call_->arguments.count > 0 ? call_->arguments[0] : nullptr;
	class_of(l_, l_.compilation_.builtin("RegExp")); // A delimiter may be one.
	const std::optional<uint32_t> array = l_.vector_class(l_.compilation_.instance_type(l_.array_));
	if (!array) {
		return refused("", Kind::Obj);
	}
	std::vector<uint32_t> list;
	list.push_back(l_.settled(l_.expression(object(), Kind::Str), later(0), position_));
	uint32_t split_by = NONE;
	if (delimiter) {
		split_by = l_.settled(l_.any_value(delimiter), later(1), position_);
	} else {
		split_by = l_.reg(Kind::Any);
		l_.emit(Op::AnyConst, position_, split_by, 1);
	}
	list.push_back(split_by);
	uint32_t limit = NONE;
	if (call_->arguments.count > 1) {
		limit = l_.expression(call_->arguments[1], Kind::F64);
	} else {
		limit = l_.reg(Kind::F64);
		l_.emit(Op::ConstF64, position_, limit, l_.number(2147483647.0));
	}
	list.push_back(limit);
	l_.emit(Op::NullCheck, position_, list[0]);
	return emit(Op::StrCall, static_cast<uint32_t>(StringFunction::Split), std::move(list), Kind::Obj);
}

uint32_t Library::number_method(Kind p_kind) {
	const Kind own = p_kind == Kind::I64 ? Kind::I64 : Kind::F64;
	const NumberMethod *method = find(NUMBER_METHODS, function_->name);
	if (function_->name == "valueOf" || (function_->name == "toString" && call_->arguments.count == 0)) {
		const uint32_t value = l_.expression(object(), p_kind);
		return function_->name == "valueOf" ? value : l_.convert(value, Kind::Str, position_);
	}
	if (!method) {
		return refused("", Kind::Str);
	}
	std::vector<uint32_t> list = arguments(l_.expression(object(), own));
	list[1] = l_.convert(list[1], Kind::F64, position_);
	return emit(Op::NumText, static_cast<uint32_t>(method->format), std::move(list), Kind::Str);
}

uint32_t Library::top_level() {
	const std::string_view name = function_->name;
	if (name == "isNaN" || name == "isFinite") {
		// NaN is the one Number unequal to itself; x - x is 0 for every finite x, and NaN otherwise.
		const uint32_t value = arguments(NONE)[0];
		const uint32_t out = l_.reg(Kind::Bool);
		if (name == "isNaN") {
			const uint32_t same = l_.reg(Kind::Bool);
			l_.emit(Op::EqF64, position_, same, value, value);
			l_.emit(Op::Not, position_, out, same);
			return out;
		}
		const uint32_t difference = l_.reg(Kind::F64), zero = l_.reg(Kind::F64);
		l_.emit(Op::SubF64, position_, difference, value, value);
		l_.emit(Op::ConstF64, position_, zero, l_.number(0));
		l_.emit(Op::EqF64, position_, out, difference, zero);
		return out;
	}
	const StringMethod *method = find(STRING_METHODS, name);
	if (method->function >= StringFunction::Escape) {
		Symbol *uri_error = l_.compilation_.builtin("URIError"); // What the decoders and encoders raise.
		if (uri_error) {
			l_.class_index(uri_error, &l_.module_.uri_error_class);
		}
	}
	return emit(Op::StrCall, static_cast<uint32_t>(method->function), arguments(NONE), method->result);
}

uint32_t Library::object_text() {
	Expr *written = object();
	const uint32_t out = l_.reg(Kind::Str);
	if (!written || written->kind == NodeKind::SuperExpr || written->kind == NodeKind::ThisExpr) {
		if (l_.fn().is_static) {
			return refused(" with no object", Kind::Str);
		}
		l_.emit(written && written->kind == NodeKind::SuperExpr ? Op::ObjectText : Op::ObjToStr, position_, out, l_.self(position_, written != nullptr));
		return out;
	}
	if (l_.untyped(written) || l_.natural(written) == Kind::Any) {
		const uint32_t value = l_.expression(written, Kind::Any);
		l_.emit(Op::AnyNullCheck, position_, value);
		l_.emit(Op::AnyText, position_, out, value);
		return out;
	}
	const uint32_t value = l_.receiver(written);
	l_.emit(Op::NullCheck, position_, value);
	l_.emit(Op::ObjToStr, position_, out, value);
	return out;
}

std::optional<uint32_t> Lowerer::library_call(Call *p_call, Symbol *p_function) {
	if (!p_function) {
		// A member of an untyped value has no symbol: toString() is the one it has here.
		Member *member = p_call->callee->kind == NodeKind::Member ? static_cast<Member *>(p_call->callee) : nullptr;
		if (member && member->name == "toString" && p_call->arguments.count == 0 && untyped(member->object)) {
			return Library(*this, p_call, nullptr).object_text();
		}
		return std::nullopt;
	}
	const Symbol *owner = p_function->owner;
	const bool builtin = p_function->unit && p_function->unit->is_builtin;
	if (!builtin || (p_function->kind != SymbolKind::Method && p_function->kind != SymbolKind::Function)) {
		return std::nullopt;
	}
	Library library(*this, p_call, p_function);
	const bool on_value = p_call->callee->kind == NodeKind::Member && !p_function->is_static();
	if (owner && owner == string_ && p_function->name == "fromCharCode") {
		return library.emit(Op::StrCall, static_cast<uint32_t>(StringFunction::FromCharCode), library.arguments(NONE, Kind::F64), Kind::Str);
	}
	if (owner && owner == string_ && on_value) {
		return library.string_method();
	}
	Kind number = Kind::Void;
	if (owner && (owner == byte_ || owner == float_ || owner == number_ || owner == int_ || owner == uint_ || owner == int64_ || owner == boolean_) && on_value &&
			kind_of_class(owner, &number)) {
		if (owner == boolean_) {
			const uint32_t value = expression(library.object(), Kind::Bool);
			return p_function->name == "valueOf" ? value : convert(value, Kind::Str, p_call->offset);
		}
		return library.number_method(number);
	}
	const std::string_view name = p_function->name;
	if (!owner && p_function->kind == SymbolKind::Function && (name == "isNaN" || name == "isFinite" || find(STRING_METHODS, name))) {
		return library.top_level();
	}
	if (owner && Library::holds(owner)) {
		if (owner->name == "RegExp") {
			return library.regexp_method();
		}
		return p_function->is_static() ? library.date_static() : library.date_method();
	}
	if (Library::is_json(owner) || (is_dictionary(owner) && name == "toJSON")) {
		return library.json_call();
	}
	if (owner && owner == object_ && name == "toString" && p_call->arguments.count == 0) {
		return library.object_text();
	}
	return std::nullopt;
}

} // namespace ir
} // namespace kas3
