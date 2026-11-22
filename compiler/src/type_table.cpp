// The types pass: the static type of every expression, and what `a.b` refers to.
//
// This pass reports only what member access can get wrong: a member that does not exist, or that the code
// may not reach. Whether a value fits where it is used is the next pass's work. A type that cannot be worked
// out is `unknown`, which never causes a further error.
//
// Recursion follows the syntax tree, which the parser keeps under about 512 levels.
#include "names.h"

#include <map>
#include <unordered_set>
#include <algorithm>
#include <cstdlib>

namespace kas3 {


const Type *Compilation::instance_type(Symbol *p_class) {
	if (!p_class) {
		return &unknown_;
	}
	if (p_class == vector_) {
		return vector_type(&any_);
	}
	auto found = instance_types_.find(p_class);
	if (found != instance_types_.end()) {
		return found->second;
	}
	types_.emplace_back();
	Type *type = &types_.back();
	type->kind = Type::Instance;
	type->symbol = p_class;
	instance_types_.emplace(p_class, type);
	return type;
}

const Type *Compilation::vector_type(const Type *p_element) {
	auto found = vector_types_.find(p_element);
	if (found != vector_types_.end()) {
		return found->second;
	}
	types_.emplace_back();
	Type *type = &types_.back();
	type->kind = Type::Vector;
	type->element = p_element;
	vector_types_.emplace(p_element, type);
	return type;
}

const Type *Compilation::class_object_type(const Type *p_instance) {
	auto found = class_object_types_.find(p_instance);
	if (found != class_object_types_.end()) {
		return found->second;
	}
	types_.emplace_back();
	Type *type = &types_.back();
	type->kind = Type::ClassObject;
	type->element = p_instance;
	class_object_types_.emplace(p_instance, type);
	return type;
}

// Type arguments nest at most Parser::MAX_TYPE_DEPTH deep.
const Type *Compilation::type_of(const TypeRef *p_type, const Type *p_element) {
	if (!p_type || p_type->form == TypeRef::Any) {
		return &any_;
	}
	if (p_type->form == TypeRef::Void) {
		return &void_;
	}
	if (!p_type->symbol) {
		return &unknown_;
	}
	if (p_type->form == TypeRef::Applied) {
		return vector_type(type_of(p_type->argument, p_element));
	}
	if (p_type->symbol == element_) {
		return p_element ? p_element : &unknown_;
	}
	return instance_type(p_type->symbol);
}

Symbol *Compilation::builtin(std::string_view p_name) const {
	if (p_name == "float") {
		return float_;
	}
	auto found = top_level_->definitions.find(p_name);
	return found != top_level_->definitions.end() && found->second->unit->is_builtin ? found->second : nullptr;
}

std::string Compilation::type_name(const Type *p_type) const {
	// Written as a loop over the wrappers, then unwound.
	std::string prefix;
	std::string suffix;
	for (;;) {
		if (!p_type) {
			return prefix + "(none)" + suffix;
		}
		switch (p_type->kind) {
			case Type::Any:
				return prefix + "*" + suffix;
			case Type::Void:
				return prefix + "void" + suffix;
			case Type::Null:
				return prefix + "null" + suffix;
			case Type::Unknown:
				return prefix + "?" + suffix;
			case Type::Instance:
				return prefix + std::string(p_type->symbol->name) + suffix;
			case Type::Vector:
				prefix += "Vector.<";
				suffix = ">" + suffix;
				break;
			case Type::ClassObject:
				prefix += "Class.<";
				suffix = ">" + suffix;
				break;
		}
		p_type = p_type->element;
	}
}

std::string Compilation::dump_types() const {
	std::string result;
	int shown = 0;
	for (const Unit *file : files_) {
		const Unit &unit = *file;
		shown += unit.is_declaration ? 0 : 1;
	}
	for (const Unit *file : files_) {
		const Unit &unit = *file;
		if (unit.is_declaration) {
			continue;
		}
		if (shown > 1) {
			result += "# " + unit.source.path() + "\n";
		}
		std::vector<TypedExpression> typed = unit.typed;
		std::stable_sort(typed.begin(), typed.end(),
				[](const TypedExpression &p_a, const TypedExpression &p_b) { return p_a.offset < p_b.offset; });
		for (const TypedExpression &item : typed) {
			Position at = unit.source.position(item.offset);
			result += std::to_string(at.line) + ":" + std::to_string(at.column) + " " + type_name(item.type) + "\n";
		}
	}
	return result;
}

} // namespace kas3
