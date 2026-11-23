// An exact engine class with no script takes its ordinary typed operation. The other arm keeps the
// original dynamic access, including its argument conversions, errors and result.
#include "lower_dynamic.h"
#include "godot_names.h"

namespace kas3::ir {

std::optional<uint32_t> Dynamic::guarded(Expr *p_expr, const Type *p_want) {
	const bool write = p_expr->kind == NodeKind::Assign;
	const bool call = p_expr->kind == NodeKind::Call;
	if (write && static_cast<Assign *>(p_expr)->op != TokenKind::Assign) {
		return std::nullopt;
	}
	Expr *access = write ? static_cast<Assign *>(p_expr)->target : call ? static_cast<Call *>(p_expr)->callee : p_expr;
	if (access->kind != NodeKind::Member) {
		return std::nullopt;
	}
	const auto *member = static_cast<Member *>(access);
	const auto *symbol = member->object->kind == NodeKind::Name ? static_cast<Name *>(member->object)->symbol : nullptr;
	if (member->resolved() || !symbol || !symbol->storage || !l_.locals_.count(symbol) || l_.kind_at(l_.locals_.at(symbol)) != Kind::Ref) {
		return std::nullopt;
	}
	Symbol *klass = symbol->storage->symbol;
	Symbol *binding = l_.compilation_.find_member(klass, member->name, false, nullptr, nullptr);
	if (!binding || binding->is_static() || !Lowerer::is_godot(binding->owner)) {
		return std::nullopt;
	}
	FunctionDecl *decl = nullptr;
	if (call && binding->kind == SymbolKind::Method && (binding->modifiers & Modifiers::Final)) {
		decl = static_cast<FunctionDecl *>(binding->node);
	} else if (!call && binding->kind == SymbolKind::Property) {
		decl = write ? binding->setter : binding->getter;
	}
	if (!decl || (call && (!decl->return_type || decl->return_type->form != TypeRef::Void)) || (!write && !call && l_.compilation_.type_of(decl->return_type) != p_want)) {
		return std::nullopt;
	}
	List<Expr *> given{};
	if (write) {
		given = { &static_cast<Assign *>(p_expr)->value, 1 };
	} else if (call) {
		given = static_cast<Call *>(p_expr)->arguments;
	}
	Kind result = Kind::Void;
	if (given.count != decl->parameters.count || (!write && !call && !l_.kind_of(p_want, &result))) {
		return std::nullopt;
	}
	for (uint32_t i = 0; i < given.count; i++) {
		Expr *arg = given[i];
		Kind kind;
		if (!l_.value_kind(arg, &kind) || arg->type != l_.compilation_.type_of(decl->parameters[i].type) ||
				(kind != Kind::Bool && kind != Kind::I32 && kind != Kind::F64 && kind != Kind::Val)) {
			return std::nullopt;
		}
	}
	uint32_t entry, class_entry;
	if (!l_.engine_name(binding->owner, binding->name, &entry) || !l_.engine_name(klass, {}, &class_entry)) {
		return std::nullopt;
	}
	const auto words = godot_words(l_.module_.engine_names[entry].value);
	if (call ? words.size() != 5 || words[1] != "method" : words.size() != 8 || words[1] != "property" || words[write ? 5 : 3] == "-") {
		return std::nullopt;
	}
	const uint32_t pos = p_expr->offset;
	const uint32_t holder = l_.settled(l_.locals_.at(symbol), Lowerer::writer(given), pos);
	std::vector<uint32_t> args{ holder };
	std::vector<uint32_t> fallback;
	for (uint32_t i = 0; i < given.count; i++) {
		args.push_back(l_.settled(l_.expression(given[i], Kind::Void), Lowerer::writer(given, i + 1), pos));
	}
	for (size_t i = 1; i < args.size(); i++) {
		if (l_.kind_at(args[i]) == Kind::Val) {
			const uint32_t present = l_.reg(Kind::Bool);
			l_.emit(Op::HostPresent, pos, present, args[i]);
			fallback.push_back(l_.emit(Op::JumpIfNot, pos, present, 0));
		}
	}
	const uint32_t test = l_.reg(Kind::Bool);
	l_.emit(Op::HostPlain, pos, test, holder, l_.string(l_.module_.engine_names[class_entry].name));
	fallback.push_back(l_.emit(Op::JumpIfNot, pos, test, 0));
	const uint32_t out = result == Kind::Void ? NONE : l_.engine_reg(result, p_want);
	if (write) {
		l_.emit(Op::HostSet, pos, holder, entry, args[1]);
	} else if (call) {
		l_.emit(Op::HostCall, pos, NONE, entry, l_.arguments(args));
	} else {
		l_.emit(Op::HostGet, pos, out, holder, entry);
	}
	const uint32_t end = l_.emit(Op::Jump, pos, 0);
	for (uint32_t branch : fallback) {
		l_.patch(branch, l_.here());
	}
	use_classes();
	const uint32_t key = name(member->name, member->name_offset);
	if (write) {
		l_.emit(Op::DynSet, pos, holder, key, l_.convert(args[1], Kind::Any, pos));
	} else if (call) {
		args[0] = key;
		for (size_t i = 1; i < args.size(); i++) {
			args[i] = l_.convert(args[i], Kind::Any, pos);
		}
		l_.emit(Op::DynCall, pos, NONE, holder, l_.arguments(std::move(args)));
	} else {
		const uint32_t value = l_.reg(Kind::Any);
		l_.emit(Op::DynGet, pos, value, holder, key);
		l_.emit(Op::Move, pos, out, l_.from_untyped(value, p_want, result, pos));
	}
	l_.patch(end, l_.here());
	return out;
}

} // namespace kas3::ir
