// The types pass: the static type of every expression, and what `a.b` refers to.
//
// This pass reports only what member access can get wrong: a member that does not exist, or that the code
// may not reach. Whether a value fits where it is used is the next pass's work. A type that cannot be worked
// out is `unknown`, which never causes a further error.
//
// Recursion follows the syntax tree, which the parser keeps under about 512 levels.
#include "names.h"

#include <algorithm>
#include <cstdlib>

namespace kas3 {

// ---- the type table ----

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
	for (const Unit &unit : units_) {
		shown += unit.is_declaration ? 0 : 1;
	}
	for (const Unit &unit : units_) {
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

// ---- the pass ----

namespace {

std::string quoted(std::string_view p_name) {
	std::string result = "'";
	result += p_name;
	result += '\'';
	return result;
}

Symbol *find_in(const std::unordered_map<std::string_view, Symbol *> &p_table, std::string_view p_name) {
	auto found = p_table.find(p_name);
	return found == p_table.end() ? nullptr : found->second;
}

class Typer {
	Compilation &compilation_;
	Unit &unit_;
	Symbol *class_ = nullptr; // The class whose member is being typed, if any.
	bool static_ = false;
	int closures_ = 0; // How many function expressions or nested functions enclose the current code.

	const Type *any_;
	const Type *unknown_;
	const Type *int_;
	const Type *uint_;
	const Type *number_;
	const Type *boolean_;
	const Type *string_;
	const Type *array_;
	const Type *object_;
	const Type *function_;
	const Type *regexp_;

	void error(uint32_t p_offset, std::string p_message) { unit_.diagnostics.error(p_offset, std::move(p_message)); }

	bool numeric(const Type *p_type) const { return p_type == int_ || p_type == uint_ || p_type == number_; }
	// The types that AS3's arithmetic is defined on without calling into a class.
	bool primitive(const Type *p_type) const {
		return numeric(p_type) || p_type == boolean_ || p_type == string_ || p_type->kind == Type::Any ||
				p_type->kind == Type::Null;
	}

	// The one type that both fit, for `a ? b : c`, `a || b` and `a && b`.
	const Type *join(const Type *p_a, const Type *p_b) const {
		if (p_a == p_b) {
			return p_a;
		}
		if (p_a->kind == Type::Unknown || p_b->kind == Type::Unknown) {
			return unknown_;
		}
		if (numeric(p_a) && numeric(p_b)) {
			return number_;
		}
		auto nullable = [&](const Type *p_type) {
			return (p_type->kind == Type::Instance && !numeric(p_type) && p_type != boolean_) ||
					p_type->kind == Type::Vector || p_type->kind == Type::ClassObject;
		};
		if (p_a->kind == Type::Null && nullable(p_b)) {
			return p_b;
		}
		if (p_b->kind == Type::Null && nullable(p_a)) {
			return p_a;
		}
		return any_;
	}

	// ---- symbols ----

	const Type *symbol_type(Symbol *p_symbol, const Type *p_element) {
		if (!p_symbol) {
			return unknown_;
		}
		switch (p_symbol->kind) {
			case SymbolKind::Class:
			case SymbolKind::Interface:
				return compilation_.class_object_type(compilation_.instance_type(p_symbol));
			case SymbolKind::Function:
			case SymbolKind::Method:
			case SymbolKind::LocalFunction:
				return function_;
			case SymbolKind::Parameter:
				return p_symbol->is_rest ? array_ : compilation_.type_of(p_symbol->type, p_element);
			case SymbolKind::Variable:
			case SymbolKind::Field:
			case SymbolKind::Local:
			case SymbolKind::CatchVariable:
				return compilation_.type_of(p_symbol->type, p_element);
			case SymbolKind::Property:
				if (p_symbol->getter) {
					return compilation_.type_of(p_symbol->getter->return_type, p_element);
				}
				if (p_symbol->setter && p_symbol->setter->parameters.count > 0) {
					return compilation_.type_of(p_symbol->setter->parameters[0].type, p_element);
				}
				return unknown_;
			case SymbolKind::Arguments:
				return any_;
		}
		return unknown_;
	}

	bool derives_from(Symbol *p_class, Symbol *p_base) const {
		for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
			if (owner == p_base) {
				return true;
			}
		}
		return false;
	}

	// May the code being typed name this member through `.`? Reports and returns false if not.
	bool accessible(Symbol *p_member, uint32_t p_offset) {
		Symbol *owner = p_member->owner;
		if (p_member->modifiers & Modifiers::Public) {
			return true;
		}
		if (p_member->modifiers & Modifiers::Private) {
			if (class_ == owner) {
				return true;
			}
			error(p_offset, quoted(p_member->name) + " is private in " + std::string(owner->name));
			return false;
		}
		if (p_member->modifiers & Modifiers::Protected) {
			if (class_ && derives_from(class_, owner)) {
				return true;
			}
			error(p_offset, quoted(p_member->name) + " is protected in " + std::string(owner->name));
			return false;
		}
		if (owner->package == unit_.package) {
			return true;
		}
		error(p_offset, quoted(p_member->name) + " in " + std::string(owner->name) + " is not visible from this package");
		return false;
	}

	// The member of that name that an object of the class or interface has, whatever its visibility.
	Symbol *instance_member(Symbol *p_class, std::string_view p_name) {
		if (p_class->kind == SymbolKind::Class) {
			for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
				if (Symbol *member = find_in(owner->klass->members, p_name)) {
					return member;
				}
			}
			return nullptr;
		}
		// An interface: itself, everything it extends, then what every object has.
		std::vector<Symbol *> pending{ p_class };
		for (size_t i = 0; i < pending.size(); i++) {
			if (Symbol *member = find_in(pending[i]->klass->members, p_name)) {
				return member;
			}
			for (Symbol *parent : pending[i]->klass->interfaces) {
				if (parent && std::find(pending.begin(), pending.end(), parent) == pending.end()) {
					pending.push_back(parent);
				}
			}
		}
		Symbol *object = compilation_.object_class();
		return object ? find_in(object->klass->members, p_name) : nullptr;
	}

	bool members_known(Symbol *p_class) {
		if (p_class->kind == SymbolKind::Class) {
			return !compilation_.hierarchy_broken(p_class);
		}
		std::vector<Symbol *> pending{ p_class };
		for (size_t i = 0; i < pending.size(); i++) {
			if (pending[i]->klass->broken) {
				return false;
			}
			auto &parents = pending[i]->klass->interfaces;
			InterfaceDecl *decl = static_cast<InterfaceDecl *>(pending[i]->node);
			if (parents.size() != decl->extends.count) {
				return false;
			}
			for (Symbol *parent : parents) {
				if (!parent) {
					return false; // One that did not resolve.
				}
				if (std::find(pending.begin(), pending.end(), parent) == pending.end()) {
					pending.push_back(parent);
				}
			}
		}
		return true;
	}

	const Type *member_type(const Type *p_object, Member *p_member) {
		switch (p_object->kind) {
			case Type::Any:
				return any_;
			case Type::Unknown:
			case Type::Void:
			case Type::Null:
				return unknown_;
			case Type::ClassObject: {
				const Type *instance = p_object->element;
				Symbol *owner = instance->kind == Type::Vector ? compilation_.vector_class() : instance->symbol;
				if (!owner) {
					return unknown_;
				}
				Symbol *member = find_in(owner->klass->members, p_member->name);
				if (!member) {
					error(p_member->name_offset, quoted(owner->name) + " has no static member " + quoted(p_member->name));
					return unknown_;
				}
				if (!member->is_static()) {
					error(p_member->name_offset, quoted(p_member->name) + " is an instance member of " +
														 std::string(owner->name) + "; it needs an object");
					return unknown_;
				}
				if (!accessible(member, p_member->name_offset)) {
					return unknown_;
				}
				p_member->member = member;
				return symbol_type(member, nullptr);
			}
			case Type::Instance:
			case Type::Vector: {
				bool vector = p_object->kind == Type::Vector;
				Symbol *owner = vector ? compilation_.vector_class() : p_object->symbol;
				if (!owner) {
					return unknown_;
				}
				Symbol *member = instance_member(owner, p_member->name);
				if (!member) {
					if (owner->modifiers & Modifiers::Dynamic) {
						return any_; // A dynamic class takes any property. Its subclasses do not.
					}
					if (members_known(owner)) {
						error(p_member->name_offset, quoted(owner->name) + " has no member " + quoted(p_member->name));
					}
					return unknown_;
				}
				if (member->is_static()) {
					error(p_member->name_offset, quoted(p_member->name) + " is a static member of " +
														 std::string(member->owner->name) + "; write " +
														 std::string(member->owner->name) + "." + std::string(p_member->name));
					return unknown_;
				}
				if (!accessible(member, p_member->name_offset)) {
					return unknown_;
				}
				p_member->member = member;
				return symbol_type(member, vector ? p_object->element : nullptr);
			}
		}
		return unknown_;
	}

	// ---- expressions ----

	const Type *number_literal(const NumberLit *p_literal) const {
		std::string text(p_literal->text);
		bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
		if (!hex && text.find_first_of(".eE") != std::string::npos) {
			return number_;
		}
		char *end = nullptr;
		unsigned long long value = std::strtoull(text.c_str(), &end, hex ? 16 : 10);
		if (!end || *end != 0 || text.size() > 18) {
			return number_;
		}
		if (value <= 2147483647ull) {
			return int_;
		}
		return value <= 4294967295ull ? uint_ : number_;
	}

	void list(const List<Expr *> &p_list) {
		for (Expr *item : p_list) {
			expression(item);
		}
	}

	const Type *call(Call *p_call) {
		list(p_call->arguments);
		Expr *callee = p_call->callee;
		if (!callee) {
			return unknown_;
		}
		if (callee->kind == NodeKind::SuperExpr) {
			callee->type = unknown_;
			return compilation_.void_type(); // `super(...)` in a constructor.
		}
		const Type *type = expression(callee);
		Symbol *symbol = nullptr;
		const Type *element = nullptr;
		if (callee->kind == NodeKind::Name) {
			symbol = static_cast<Name *>(callee)->symbol;
		} else if (callee->kind == NodeKind::Member) {
			Member *member = static_cast<Member *>(callee);
			symbol = member->symbol ? member->symbol : member->member;
			if (member->member && member->object && member->object->type && member->object->type->kind == Type::Vector) {
				element = member->object->type->element;
			}
		}
		if (symbol && (symbol->kind == SymbolKind::Method || symbol->kind == SymbolKind::Function ||
							  symbol->kind == SymbolKind::LocalFunction)) {
			return compilation_.type_of(static_cast<FunctionDecl *>(symbol->node)->return_type, element);
		}
		switch (type->kind) {
			case Type::ClassObject:
				return type->element; // `int(x)`, `Player(x)`: a conversion.
			case Type::Any:
				return any_;
			case Type::Instance:
				return type == function_ ? any_ : unknown_;
			default:
				return unknown_;
		}
	}

	const Type *unary(Unary *p_unary) {
		const Type *operand = expression(p_unary->operand);
		switch (p_unary->op) {
			case TokenKind::Bang:
			case TokenKind::KwDelete:
				return boolean_;
			case TokenKind::KwTypeof:
				return string_;
			case TokenKind::KwVoid:
				return any_;
			case TokenKind::Tilde:
				return primitive(operand) ? int_ : unknown_;
			case TokenKind::PlusPlus:
			case TokenKind::MinusMinus:
				return numeric(operand) ? operand : (primitive(operand) ? number_ : unknown_);
			case TokenKind::Minus:
				if (p_unary->operand && p_unary->operand->kind == NodeKind::NumberLit) {
					// `-1` is an int, and so is -2147483648, whose digits alone are not.
					std::string_view text = static_cast<NumberLit *>(p_unary->operand)->text;
					if (operand == int_ || text == "2147483648") {
						return int_;
					}
				}
				return primitive(operand) ? number_ : unknown_;
			case TokenKind::Plus:
				return primitive(operand) ? number_ : unknown_;
			default:
				return unknown_;
		}
	}

	const Type *arithmetic(TokenKind p_op, const Type *p_left, const Type *p_right) {
		if (p_left->kind == Type::Unknown || p_right->kind == Type::Unknown) {
			return unknown_;
		}
		// On anything but the primitives the result depends on the class. Godot's math types get their
		// rules in a later stage; until then the type is unknown.
		if (!primitive(p_left) || !primitive(p_right)) {
			return unknown_;
		}
		switch (p_op) {
			case TokenKind::Plus:
			case TokenKind::PlusAssign:
				if (p_left == string_ || p_right == string_) {
					return string_;
				}
				if (p_left->kind == Type::Any || p_right->kind == Type::Any) {
					return any_; // A string or a number, decided when it runs.
				}
				return number_;
			case TokenKind::Amp:
			case TokenKind::Pipe:
			case TokenKind::Caret:
			case TokenKind::ShiftLeft:
			case TokenKind::ShiftRight:
				return int_;
			case TokenKind::ShiftRightUnsigned:
				return uint_;
			default:
				// AS3 computes int + int as a Number; the wrap to 32 bits happens when it is stored in an int.
				return number_;
		}
	}

	const Type *binary(Binary *p_binary) {
		const Type *left = expression(p_binary->left);
		const Type *right = expression(p_binary->right);
		switch (p_binary->op) {
			case TokenKind::Equal:
			case TokenKind::NotEqual:
			case TokenKind::StrictEqual:
			case TokenKind::StrictNotEqual:
			case TokenKind::Less:
			case TokenKind::Greater:
			case TokenKind::LessEqual:
			case TokenKind::GreaterEqual:
			case TokenKind::KwIs:
			case TokenKind::KwIn:
			case TokenKind::KwInstanceof:
				return boolean_;
			case TokenKind::KwAs:
				if (right->kind == Type::ClassObject) {
					return right->element;
				}
				return right->kind == Type::Unknown ? unknown_ : any_;
			case TokenKind::AmpAmp:
			case TokenKind::PipePipe:
				return join(left, right);
			case TokenKind::Comma:
				return right;
			default:
				return arithmetic(p_binary->op, left, right);
		}
	}

	const Type *compute(Expr *p_expression) {
		switch (p_expression->kind) {
			case NodeKind::NumberLit:
				return number_literal(static_cast<NumberLit *>(p_expression));
			case NodeKind::StringLit:
				return string_;
			case NodeKind::RegexLit:
				return regexp_;
			case NodeKind::BoolLit:
				return boolean_;
			case NodeKind::NullLit:
				return compilation_.null_type();
			case NodeKind::ThisExpr:
				if (static_) {
					return unknown_; // Reported by the names pass.
				}
				// In a nested function, `this` is whatever the caller supplies.
				return class_ && closures_ == 0 ? compilation_.instance_type(class_) : any_;
			case NodeKind::SuperExpr:
				return class_ && !static_ && class_->klass->base ? compilation_.instance_type(class_->klass->base)
																 : unknown_;
			case NodeKind::Name:
				return symbol_type(static_cast<Name *>(p_expression)->symbol, nullptr);
			case NodeKind::ArrayLit:
				list(static_cast<ArrayLit *>(p_expression)->elements);
				return array_;
			case NodeKind::ObjectLit:
				for (ObjectField &field : static_cast<ObjectLit *>(p_expression)->fields) {
					expression(field.value);
				}
				return object_;
			case NodeKind::VectorLit: {
				VectorLit *literal = static_cast<VectorLit *>(p_expression);
				list(literal->elements);
				return compilation_.vector_type(compilation_.type_of(literal->element_type));
			}
			case NodeKind::FunctionExpr:
				closures_++;
				function(static_cast<FunctionExpr *>(p_expression)->function);
				closures_--;
				return function_;
			case NodeKind::Member: {
				Member *member = static_cast<Member *>(p_expression);
				if (member->symbol) {
					return symbol_type(member->symbol, nullptr); // A definition named by its package.
				}
				return member_type(expression(member->object), member);
			}
			case NodeKind::Index: {
				Index *index = static_cast<Index *>(p_expression);
				const Type *object = expression(index->object);
				expression(index->index);
				if (object->kind == Type::Vector) {
					return object->element;
				}
				return object->kind == Type::Unknown ? unknown_ : any_;
			}
			case NodeKind::TypeApply: {
				TypeApply *apply = static_cast<TypeApply *>(p_expression);
				const Type *base = expression(apply->base);
				if (base->kind == Type::ClassObject && base->element->kind == Type::Vector) {
					return compilation_.class_object_type(compilation_.vector_type(compilation_.type_of(apply->argument)));
				}
				return unknown_;
			}
			case NodeKind::Call:
				return call(static_cast<Call *>(p_expression));
			case NodeKind::New: {
				New *create = static_cast<New *>(p_expression);
				list(create->arguments);
				const Type *callee = expression(create->callee);
				if (callee->kind == Type::ClassObject) {
					return callee->element;
				}
				return callee->kind == Type::Any ? any_ : unknown_;
			}
			case NodeKind::Unary:
				return unary(static_cast<Unary *>(p_expression));
			case NodeKind::Postfix: {
				const Type *operand = expression(static_cast<Postfix *>(p_expression)->operand);
				return numeric(operand) ? operand : (primitive(operand) ? number_ : unknown_);
			}
			case NodeKind::Binary:
				return binary(static_cast<Binary *>(p_expression));
			case NodeKind::Assign: {
				Assign *assign = static_cast<Assign *>(p_expression);
				const Type *target = expression(assign->target);
				const Type *value = expression(assign->value);
				if (assign->op == TokenKind::Assign || numeric(target)) {
					return target; // The value, converted to what it is stored in.
				}
				if (assign->op == TokenKind::AmpAmpAssign || assign->op == TokenKind::PipePipeAssign) {
					return target;
				}
				return arithmetic(assign->op, target, value);
			}
			case NodeKind::Conditional: {
				Conditional *choice = static_cast<Conditional *>(p_expression);
				expression(choice->condition);
				const Type *then_type = expression(choice->then_value);
				const Type *else_type = expression(choice->else_value);
				return join(then_type, else_type);
			}
			default:
				return unknown_;
		}
	}

	const Type *expression(Expr *p_expression) {
		if (!p_expression) {
			return unknown_;
		}
		p_expression->type = compute(p_expression);
		return p_expression->type;
	}

	// An expression whose type `--dump-types` shows.
	void shown(Expr *p_expression) {
		if (p_expression) {
			unit_.typed.push_back({ p_expression->offset, expression(p_expression) });
		}
	}

	// ---- statements ----

	void declarators(VarDecl *p_decl) {
		for (Declarator &declarator : p_decl->declarators) {
			shown(declarator.initializer);
		}
	}

	void statement(Stmt *p_statement) {
		if (!p_statement) {
			return;
		}
		switch (p_statement->kind) {
			case NodeKind::Block:
				for (Stmt *inner : static_cast<Block *>(p_statement)->statements) {
					statement(inner);
				}
				break;
			case NodeKind::ExprStmt:
				shown(static_cast<ExprStmt *>(p_statement)->expr);
				break;
			case NodeKind::VarStmt:
				declarators(static_cast<VarStmt *>(p_statement)->decl);
				break;
			case NodeKind::If: {
				If *branch = static_cast<If *>(p_statement);
				expression(branch->condition);
				statement(branch->then_branch);
				statement(branch->else_branch);
			} break;
			case NodeKind::While:
				expression(static_cast<While *>(p_statement)->condition);
				statement(static_cast<While *>(p_statement)->body);
				break;
			case NodeKind::DoWhile:
				statement(static_cast<DoWhile *>(p_statement)->body);
				expression(static_cast<DoWhile *>(p_statement)->condition);
				break;
			case NodeKind::For: {
				For *loop = static_cast<For *>(p_statement);
				statement(loop->init);
				expression(loop->condition);
				expression(loop->update);
				statement(loop->body);
			} break;
			case NodeKind::ForIn: {
				ForIn *loop = static_cast<ForIn *>(p_statement);
				expression(loop->target);
				expression(loop->collection);
				statement(loop->body);
			} break;
			case NodeKind::Switch: {
				Switch *choice = static_cast<Switch *>(p_statement);
				expression(choice->subject);
				for (SwitchCase &branch : choice->cases) {
					expression(branch.test);
					for (Stmt *inner : branch.body) {
						statement(inner);
					}
				}
			} break;
			case NodeKind::Return:
				expression(static_cast<Return *>(p_statement)->value);
				break;
			case NodeKind::Throw:
				expression(static_cast<Throw *>(p_statement)->value);
				break;
			case NodeKind::Try: {
				Try *attempt = static_cast<Try *>(p_statement);
				statement(attempt->body);
				for (CatchClause &clause : attempt->catches) {
					statement(clause.body);
				}
				statement(attempt->finally_block);
			} break;
			case NodeKind::Labeled:
				statement(static_cast<Labeled *>(p_statement)->body);
				break;
			case NodeKind::FunctionStmt:
				closures_++;
				function(static_cast<FunctionStmt *>(p_statement)->function);
				closures_--;
				break;
			default:
				break;
		}
	}

	void function(FunctionDecl *p_function) {
		for (Parameter &parameter : p_function->parameters) {
			expression(parameter.default_value);
		}
		statement(p_function->body);
	}

	void definition(Node *p_node) {
		switch (p_node->kind) {
			case NodeKind::ClassDecl: {
				ClassDecl *decl = static_cast<ClassDecl *>(p_node);
				class_ = decl->symbol;
				for (Node *member : decl->members) {
					if (member->kind == NodeKind::VarDecl) {
						static_ = (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static) != 0;
						declarators(static_cast<VarDecl *>(member));
					} else if (member->kind == NodeKind::FunctionDecl) {
						static_ = (static_cast<FunctionDecl *>(member)->modifiers.bits & Modifiers::Static) != 0;
						function(static_cast<FunctionDecl *>(member));
					}
				}
				class_ = nullptr;
				static_ = false;
			} break;
			case NodeKind::InterfaceDecl:
				for (FunctionDecl *method : static_cast<InterfaceDecl *>(p_node)->methods) {
					function(method);
				}
				break;
			case NodeKind::FunctionDecl:
				function(static_cast<FunctionDecl *>(p_node));
				break;
			case NodeKind::VarDecl:
				declarators(static_cast<VarDecl *>(p_node));
				break;
			default:
				break;
		}
	}

	const Type *builtin(const char *p_name) {
		Symbol *symbol = compilation_.builtin(p_name);
		return symbol ? compilation_.instance_type(symbol) : compilation_.unknown_type();
	}

public:
	Typer(Compilation &p_compilation, Unit &p_unit) :
			compilation_(p_compilation), unit_(p_unit) {
		any_ = compilation_.any_type();
		unknown_ = compilation_.unknown_type();
		int_ = builtin("int");
		uint_ = builtin("uint");
		number_ = builtin("Number");
		boolean_ = builtin("Boolean");
		string_ = builtin("String");
		array_ = builtin("Array");
		object_ = builtin("Object");
		function_ = builtin("Function");
		regexp_ = builtin("RegExp");
	}

	void run() {
		Program *tree = unit_.tree;
		if (tree->package) {
			for (Node *node : tree->package->definitions) {
				definition(node);
			}
		}
		for (Node *node : tree->definitions) {
			definition(node);
		}
	}
};

} // namespace

void type_bodies(Compilation &p_compilation) {
	for (Unit &unit : p_compilation.units()) {
		Typer(p_compilation, unit).run();
	}
}

} // namespace kas3
