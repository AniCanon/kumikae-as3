#include "lowerer.h"

namespace kas3 {
namespace ir {

uint32_t Lowerer::emit(Op p_op, uint32_t p_position, uint32_t p_a, uint32_t p_b, uint32_t p_c) {
	Instruction instruction;
	instruction.op = p_op;
	instruction.a = p_a;
	instruction.b = p_b;
	instruction.c = p_c;
	instruction.position = p_position;
	fn().code.push_back(instruction);
	return (uint32_t)fn().code.size() - 1;
}

void Lowerer::patch(uint32_t p_jump, uint32_t p_target) {
	Instruction &instruction = fn().code[p_jump];
	(instruction.op == Op::Jump ? instruction.a : instruction.b) = p_target;
}

uint32_t Lowerer::reg(Kind p_kind) {
	fn().registers.push_back(p_kind);
	return (uint32_t)fn().registers.size() - 1;
}

uint32_t Lowerer::engine_reg(Kind p_kind, const Type *p_type) {
	std::optional<std::string> godot;
	if (is_engine(p_kind) && p_type && p_type->kind == Type::Instance && p_type->symbol && is_godot(p_type->symbol)) {
		godot = godot_name(p_type->symbol->name, std::string_view());
	}
	// One of Godot's values of a type not known here (what gives back anything, a null) may be any of them.
	return engine_reg(p_kind, godot ? std::string_view(*godot) : p_kind == Kind::Val ? ANY_GODOT_TYPE : std::string_view());
}

uint32_t Lowerer::engine_reg(Kind p_kind, std::string_view p_godot) {
	uint32_t out = reg(p_kind);
	if (is_engine(p_kind) && !p_godot.empty()) {
		if (fn().engine_types.size() <= out) {
			fn().engine_types.resize(out + 1);
		}
		fn().engine_types[out] = std::string(p_godot);
	}
	return out;
}

uint32_t Lowerer::reg_like(uint32_t p_reg) {
	// A copy: making the register may move the list it is in.
	const std::string type = p_reg < fn().engine_types.size() ? fn().engine_types[p_reg] : std::string();
	return engine_reg(kind_at(p_reg), type);
}

Kind Lowerer::kind_at(uint32_t p_reg) const {
	return p_reg < fn().registers.size() ? fn().registers[p_reg] : Kind::Void;
}

uint32_t Lowerer::arguments(std::vector<uint32_t> p_list) {
	fn().argument_lists.push_back(std::move(p_list));
	return (uint32_t)fn().argument_lists.size() - 1;
}

bool Lowerer::writes_locals(Expr *p_expression) {
	bool writes = false;
	walk(p_expression, [&](Node *p_node) {
		if (p_node->kind == NodeKind::Assign || p_node->kind == NodeKind::Postfix) {
			writes = true;
		} else if (p_node->kind == NodeKind::Unary) {
			TokenKind op = static_cast<Unary *>(p_node)->op;
			writes = writes || op == TokenKind::PlusPlus || op == TokenKind::MinusMinus;
		}
	});
	return writes;
}

uint32_t Lowerer::settled(uint32_t p_reg, Expr *p_later, uint32_t p_position) {
	if (!p_later || !writes_locals(p_later)) {
		return p_reg;
	}
	for (const auto &local : locals_) {
		if (local.second == p_reg) {
			uint32_t copy = reg_like(p_reg);
			emit(Op::Move, p_position, copy, p_reg);
			return copy;
		}
	}
	return p_reg;
}

} // namespace ir
} // namespace kas3
