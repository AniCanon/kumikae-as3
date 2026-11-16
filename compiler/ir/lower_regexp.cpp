#include "library_names.h"
#include "lower_functions.h"
#include "lower_library.h"

namespace kas3 {
namespace ir {

namespace {

bool is_builtin_class(const Symbol *p_class, std::string_view p_name) {
	return p_class && p_class->kind == SymbolKind::Class && p_class->name == p_name && p_class->unit && p_class->unit->is_builtin &&
			(!p_class->package || p_class->package->name.empty());
}

} // namespace

bool Library::holds(const Symbol *p_class) {
	return is_builtin_class(p_class, "RegExp") || is_builtin_class(p_class, "Date");
}

std::optional<uint32_t> Library::class_of(Lowerer &p_lowerer, const Symbol *p_class) {
	if (!holds(p_class)) {
		return std::nullopt;
	}
	if (const auto found = p_lowerer.classes_.find(p_class); found != p_lowerer.classes_.end()) {
		return found->second;
	}
	Module &module = p_lowerer.module_;
	const bool regexp = p_class->name == "RegExp";
	Class klass;
	klass.name = std::string(p_class->name);
	if (regexp) {
		klass.fields = { { "source", Kind::Str }, { "flags", Kind::I32 }, { "lastIndex", Kind::F64 } };
	} else {
		klass.fields = { { "time", Kind::F64 } };
	}
	module.classes.push_back(std::move(klass));
	const uint32_t index = static_cast<uint32_t>(module.classes.size() - 1);
	p_lowerer.classes_[p_class] = index;
	(regexp ? module.regexp_class : module.date_class) = index;
	if (regexp) {
		// lastIndex is read and written as the field it is.
		if (const auto member = p_class->klass->members.find("lastIndex"); member != p_class->klass->members.end()) {
			p_lowerer.fields_[member->second] = REGEXP_LAST_INDEX;
		}
		if (Symbol *syntax_error = p_lowerer.compilation_.builtin("SyntaxError")) {
			p_lowerer.class_index(syntax_error, &module.syntax_error_class);
		}
	}
	return index;
}

// `/a+b/gi`: a new RegExp each time it is reached, as AS3 compiles a literal to `new RegExp`.
uint32_t Library::regex_literal(Lowerer &p_lowerer, RegexLit *p_literal) {
	Lowerer &l = p_lowerer;
	class_of(l, l.compilation_.builtin("RegExp"));
	const uint32_t position = p_literal->offset;
	const std::string_view text = p_literal->text;
	const size_t end = text.rfind('/');
	const uint32_t source = l.reg(Kind::Str), flags = l.reg(Kind::Str);
	l.emit(Op::ConstStr, position, source, l.string(std::string(text.substr(1, end - 1))));
	l.emit(Op::ConstStr, position, flags, l.string(std::string(text.substr(end + 1))));
	const uint32_t out = l.reg(Kind::Obj);
	l.emit(Op::RegExpCall, position, out, static_cast<uint32_t>(RegExpFunction::New), l.arguments({ source, flags }));
	return out;
}

// `new RegExp(source, flags)` and `new Date(...)`. An argument left out of RegExp's is undefined, which is
// no source and no flags: the declaration's null would be the text "null".
std::optional<uint32_t> Library::construct(Lowerer &p_lowerer, New *p_new) {
	Lowerer &l = p_lowerer;
	const Symbol *symbol = l.named_class(p_new->callee);
	if (!class_of(l, symbol)) {
		return std::nullopt;
	}
	if (symbol->name == "Date") {
		return date_new(l, p_new);
	}
	const uint32_t position = p_new->offset;
	std::vector<uint32_t> list;
	for (uint32_t i = 0; i < 2; i++) {
		if (i < p_new->arguments.count) {
			Expr *after = i + 1 < p_new->arguments.count ? p_new->arguments[i + 1] : nullptr;
			list.push_back(l.settled(l.any_value(p_new->arguments[i]), after, position));
		} else {
			list.push_back(l.reg(Kind::Any));
			l.emit(Op::AnyConst, position, list.back(), 0);
		}
	}
	const uint32_t out = l.reg(Kind::Obj);
	l.emit(Op::RegExpCall, position, out, static_cast<uint32_t>(RegExpFunction::New), l.arguments(std::move(list)));
	return out;
}

// A property of a RegExp or a Date. lastIndex is a field, which the lowerer reaches itself once the class
// is made.
std::optional<Place> Library::place(Lowerer &p_lowerer, Expr *p_target, Expr *p_later) {
	Member *member = p_target->kind == NodeKind::Member ? static_cast<Member *>(p_target) : nullptr;
	const Symbol *symbol = member ? member->resolved() : nullptr;
	if (!symbol || !symbol->owner || !class_of(p_lowerer, symbol->owner) || !symbol->is_instance_member()) {
		return std::nullopt;
	}
	if (symbol->owner->name == "Date") {
		return date_place(p_lowerer, member, p_later);
	}
	const RegExpProperty *property = regexp_property(symbol->name);
	if (!property) {
		return std::nullopt;
	}
	Place place;
	place.where = Place::Library;
	place.op = Op::RegExpCall;
	place.index = static_cast<uint32_t>(property->function);
	place.kind = property->kind;
	place.object = p_lowerer.settled(p_lowerer.receiver(member->object), p_later, member->name_offset);
	return place;
}

bool Library::own_value_of(const Expr *p_value) {
	const Type *type = p_value->type;
	for (const Symbol *at = type && type->kind == Type::Instance ? type->symbol : nullptr; at && at->klass; at = at->klass->base) {
		if (at->unit && at->unit->is_builtin) {
			return false;
		}
		if (at->klass->members.count("valueOf")) {
			return true;
		}
	}
	return false;
}

uint32_t Library::load(Lowerer &p_lowerer, const Place &p_place, uint32_t p_position) {
	const uint32_t out = p_lowerer.reg(p_place.kind);
	p_lowerer.emit(p_place.op, p_position, out, p_place.index, p_lowerer.arguments({ p_place.object }));
	return out;
}

void Library::store(Lowerer &p_lowerer, const Place &p_place, uint32_t p_value, uint32_t p_position) {
	if (p_place.key == NONE) {
		p_lowerer.refuse(p_position, "writing a property that is only read");
		return;
	}
	const uint32_t out = p_lowerer.reg(Kind::F64);
	p_lowerer.emit(p_place.op, p_position, out, p_place.key, p_lowerer.arguments({ p_place.object, p_value }));
}

uint32_t Library::regexp_method() {
	const std::optional<RegExpFunction> which = ir::regexp_method(function_->name);
	if (!which) {
		return refused("", Kind::Any);
	}
	class_of(l_, function_->owner);
	const bool exec = *which == RegExpFunction::Exec;
	if (exec) {
		l_.vector_class(l_.compilation_.instance_type(l_.array_)); // What exec() gives.
	}
	std::vector<uint32_t> list = arguments(l_.receiver(object()));
	return emit(Op::RegExpCall, static_cast<uint32_t>(*which), std::move(list), exec ? Kind::Any : Kind::Bool);
}

// match(pattern), search(pattern), replace(pattern, replacement): each untyped, as String's declarations
// have them, and what each is (a RegExp, a function, or text) is found as the call runs. One left out is
// undefined, as in ECMAScript.
uint32_t Library::pattern_call(RegExpFunction p_which) {
	class_of(l_, l_.compilation_.builtin("RegExp"));
	std::vector<uint32_t> list;
	list.push_back(l_.settled(l_.expression(object(), Kind::Str), later(0), position_));
	const uint32_t count = p_which == RegExpFunction::Replace ? 2 : 1;
	for (uint32_t i = 0; i < count; i++) {
		if (i < call_->arguments.count) {
			list.push_back(l_.settled(l_.any_value(call_->arguments[i]), later(i + 1), position_));
		} else {
			list.push_back(l_.reg(Kind::Any));
			l_.emit(Op::AnyConst, position_, list.back(), 0);
		}
	}
	l_.emit(Op::NullCheck, position_, list[0]);
	const Kind result = p_which == RegExpFunction::Match ? Kind::Obj : p_which == RegExpFunction::Search ? Kind::I32 : Kind::Str;
	if (p_which == RegExpFunction::Match) {
		l_.vector_class(l_.compilation_.instance_type(l_.array_));
	}
	if (p_which == RegExpFunction::Replace) {
		Functions(l_).use_argument_error(); // A replacement function called with more than it takes.
	}
	return emit(Op::RegExpCall, static_cast<uint32_t>(p_which), std::move(list), result);
}

} // namespace ir
} // namespace kas3
