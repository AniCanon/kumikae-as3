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
	FunctionDecl *current_ = nullptr; // The innermost function being typed.
	mutable std::map<std::pair<Symbol *, Symbol *>, std::pair<bool, bool>> related_; // is_a's answers.
	bool leaves_ = false; // It has a `return <value>` or a `throw` somewhere in its own body.

	const Type *any_;
	const Type *unknown_;
	const Type *int_;
	const Type *uint_;
	const Type *number_;
	const Type *boolean_;
	const Type *string_;
	const Type *array_;
	const Type *object_;
	const Type *dictionary_ = nullptr; // flash.utils.Dictionary
	Package *godot_ = nullptr; // Godot's API, if the program has it.
	const Type *function_;
	const Type *regexp_;
	const Type *class_type_;

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

	// ---- conversions ----

	enum class Fit {
		Yes,
		Cast, // The value may be of the type, but only a cast can say so.
		No,
	};

	// Whether an object of p_class is always one of p_target: the class, a base, or an interface that it,
	// a base, or one of those interfaces declares. *r_unsure is set if part of the hierarchy is not known.
	bool is_a(Symbol *p_class, Symbol *p_target, bool *r_unsure) const {
		// Asked again and again for the same pair, and a hierarchy can be deep.
		auto known = related_.find({ p_class, p_target });
		if (known != related_.end()) {
			*r_unsure |= known->second.second;
			return known->second.first;
		}
		bool unsure = false;
		bool result = walk_is_a(p_class, p_target, &unsure);
		related_[{ p_class, p_target }] = { result, unsure };
		*r_unsure |= unsure;
		return result;
	}

	bool walk_is_a(Symbol *p_class, Symbol *p_target, bool *r_unsure) const {
		std::vector<Symbol *> pending;
		std::unordered_set<Symbol *> seen;
		for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
			if (owner->klass->broken || owner->klass->incomplete) {
				*r_unsure = true;
			}
			pending.push_back(owner);
			if (owner->kind != SymbolKind::Class) {
				break;
			}
		}
		for (size_t i = 0; i < pending.size(); i++) {
			if (pending[i] == p_target) {
				return true;
			}
			if (pending[i]->klass->broken) {
				*r_unsure = true;
			}
			for (Symbol *parent : pending[i]->klass->interfaces) {
				if (!parent) {
					*r_unsure = true; // One that did not resolve.
				} else if (seen.insert(parent).second) {
					pending.push_back(parent);
				}
			}
		}
		return false;
	}

	// May a value of type p_from be used where p_to is expected, with no cast written?
	// True if the type, or an element type inside it, did not resolve.
	static bool unresolved(const Type *p_type) {
		for (; p_type; p_type = p_type->element) {
			if (p_type->kind == Type::Unknown) {
				return true;
			}
		}
		return false;
	}

	// AS3's own collections are converted when they are handed to the engine: an Array or a Vector becomes
	// a Godot array, a Vector becomes the packed array of its element type, and an Object or a
	// flash.utils.Dictionary becomes a Godot dictionary. Nothing converts on the way back.
	bool converts_to_engine(const Type *p_from, const Type *p_to) const {
		auto in_godot = [&](const Type *p_type) {
			return godot_ && p_type->kind == Type::Instance && p_type->symbol && p_type->symbol->package == godot_;
		};
		if (!in_godot(p_to)) {
			return false;
		}
		std::string_view name = p_to->symbol->name;
		if (name == "GodotArray") {
			return p_from == array_ || p_from->kind == Type::Vector;
		}
		if (name == "GodotDictionary") {
			return p_from == object_ || p_from == dictionary_;
		}
		const std::string_view head = "Packed";
		const std::string_view tail = "Array";
		if (name.size() <= head.size() + tail.size() || name.substr(0, head.size()) != head ||
				name.substr(name.size() - tail.size()) != tail) {
			return false;
		}
		if (p_from == array_) {
			return true; // Its elements are checked when it is converted.
		}
		if (p_from->kind != Type::Vector) {
			return false;
		}
		std::string_view element = name.substr(head.size(), name.size() - head.size() - tail.size());
		const Type *from = p_from->element;
		if (element == "Byte" || element == "Int32" || element == "Int64" || element == "Float32" ||
				element == "Float64") {
			return numeric(from);
		}
		if (element == "String") {
			return from == string_;
		}
		return in_godot(from) && from->symbol->name == element; // Vector2, Vector3, Vector4, Color.
	}

	Fit fits(const Type *p_from, const Type *p_to) const {
		if (p_from == p_to || p_from->kind == Type::Any || p_to->kind == Type::Any || p_to->kind == Type::Void ||
				unresolved(p_from) || unresolved(p_to)) {
			return Fit::Yes;
		}
		if (p_from->kind == Type::Void) {
			return Fit::No;
		}
		// Anything converts to a Boolean, everything is an Object, and the number types convert to each
		// other. Adobe's compiler only warns when null is stored in a number, so nothing is said here.
		if (p_to == boolean_ || p_to == object_ || (numeric(p_from) && numeric(p_to)) || p_from->kind == Type::Null) {
			return Fit::Yes;
		}
		if (converts_to_engine(p_from, p_to)) {
			return Fit::Yes;
		}
		if (p_from->kind == Type::ClassObject) {
			return p_to == class_type_ ? Fit::Yes : Fit::No;
		}
		if (p_to->kind == Type::ClassObject || p_to->kind == Type::Null) {
			return Fit::Yes; // Not a type that can be written.
		}
		if (p_from->kind == Type::Vector || p_to->kind == Type::Vector) {
			if (p_from->kind == Type::Vector && p_to->kind == Type::Vector) {
				// Vectors of different element types are unrelated, even when the elements are related.
				return Fit::No;
			}
			return p_from == object_ ? Fit::Cast : Fit::No;
		}
		Symbol *from = p_from->symbol;
		Symbol *to = p_to->symbol;
		if (!from || !to) {
			return Fit::Yes;
		}
		bool unsure = false;
		if (is_a(from, to, &unsure)) {
			return Fit::Yes;
		}
		bool narrower = p_from == object_ || is_a(to, from, &unsure);
		if (unsure) {
			return Fit::Yes;
		}
		return narrower ? Fit::Cast : Fit::No;
	}

	// Reports a value that does not fit where it is stored or passed.
	void store(Expr *p_value, const Type *p_to) {
		// Which type Adobe's compiler gives `a = b` used as a value is not known, so it is not judged.
		if (!p_value || !p_value->type || !p_to || p_value->kind == NodeKind::Assign) {
			return;
		}
		switch (fits(p_value->type, p_to)) {
			case Fit::Yes:
				break;
			case Fit::Cast:
				error(p_value->offset, "a value of type " + quoted(compilation_.type_name(p_value->type)) +
											   " may not be a " + quoted(compilation_.type_name(p_to)) +
											   "; use 'as' or a cast");
				break;
			case Fit::No:
				error(p_value->offset, "can't use a value of type " + quoted(compilation_.type_name(p_value->type)) +
											   " where " + quoted(compilation_.type_name(p_to)) + " is expected");
				break;
		}
	}

	// Reports a write to something that can't be written: `x = ...`, `x += ...`, `x++`.
	void written(Expr *p_target) {
		if (!p_target) {
			return;
		}
		Symbol *symbol = nullptr;
		uint32_t offset = p_target->offset;
		bool own_object = true; // Named bare or through `this`.
		if (p_target->kind == NodeKind::Name) {
			symbol = static_cast<Name *>(p_target)->symbol;
		} else if (p_target->kind == NodeKind::Member) {
			Member *member = static_cast<Member *>(p_target);
			symbol = member->symbol ? member->symbol : member->member;
			offset = member->name_offset;
			own_object = member->object && member->object->kind == NodeKind::ThisExpr;
		}
		if (!symbol) {
			return;
		}
		std::string name = quoted(symbol->name);
		switch (symbol->kind) {
			case SymbolKind::Field:
			case SymbolKind::Variable:
			case SymbolKind::Local: {
				if (!symbol->is_const) {
					break;
				}
				// An instance constant gets its value in the constructor of its own class.
				bool constructing = symbol->kind == SymbolKind::Field && !symbol->is_static() && own_object &&
						closures_ == 0 && class_ == symbol->owner && class_->klass->constructor &&
						class_->klass->constructor->node == current_;
				if (!constructing) {
					error(offset, "can't assign to the constant " + name);
				}
			} break;
			case SymbolKind::Property:
				// The setter may be in a base class that did not resolve, or in another interface.
				if (symbol->getter && !symbol->setter && symbol->owner && symbol->owner->kind == SymbolKind::Class &&
						!compilation_.hierarchy_broken(symbol->owner)) {
					error(offset, name + " is read-only");
				}
				break;
			case SymbolKind::Method:
				// A built-in class's methods may be on its prototype, where they can be replaced.
				if (!symbol->unit->is_builtin) {
					error(offset, "can't assign to the method " + name);
				}
				break;
			case SymbolKind::Function:
				error(offset, "can't assign to the function " + name);
				break;
			case SymbolKind::Class:
			case SymbolKind::Interface:
				error(offset, "can't assign to the class " + name);
				break;
			default:
				break;
		}
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
			// None. A static member of that name is returned so that the error can say so.
			for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
				if (Symbol *member = find_in(owner->klass->statics, p_name)) {
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
				Symbol *member = find_in(owner->klass->statics, p_member->name);
				if (!member) {
					if (find_in(owner->klass->members, p_member->name)) {
						error(p_member->name_offset, quoted(p_member->name) + " is an instance member of " +
															 std::string(owner->name) + "; it needs an object");
					} else {
						error(p_member->name_offset,
								quoted(owner->name) + " has no static member " + quoted(p_member->name));
					}
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
		unsigned long long value = 0;
		if (!whole_value(p_literal, &value)) {
			return number_;
		}
		if (value <= 2147483647ull) {
			return int_;
		}
		return value <= 4294967295ull ? uint_ : number_;
	}

	// The value of a literal written as a whole number, if it is small enough to matter here.
	bool whole_value(const NumberLit *p_literal, unsigned long long *r_value) const {
		std::string text(p_literal->text);
		bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
		if (!hex && text.find_first_of(".eE") != std::string::npos) {
			return false;
		}
		size_t start = hex ? 2 : 0;
		while (start + 1 < text.size() && text[start] == '0') {
			start++;
		}
		std::string digits = text.substr(start);
		if (digits.empty() || digits.size() > 15) {
			return false;
		}
		char *end = nullptr;
		*r_value = std::strtoull(digits.c_str(), &end, hex ? 16 : 10);
		return end && *end == 0;
	}

	void list(const List<Expr *> &p_list) {
		for (Expr *item : p_list) {
			expression(item);
		}
	}

	static std::string counted(size_t p_count, const char *p_noun) {
		return std::to_string(p_count) + " " + p_noun + (p_count == 1 ? "" : "s");
	}

	// Checks the arguments of a call against the function's parameters: how many, then each one's type.
	void arguments(FunctionDecl *p_function, std::string_view p_name, const List<Expr *> &p_arguments,
			uint32_t p_offset, const Type *p_element) {
		size_t required = 0;
		size_t most = 0;
		bool rest = false;
		for (const Parameter &parameter : p_function->parameters) {
			if (parameter.is_rest) {
				rest = true;
				break;
			}
			most++;
			if (!parameter.default_value) {
				required = most;
			}
		}
		size_t given = p_arguments.count;
		if (given < required || (given > most && !rest)) {
			std::string takes;
			if (rest || (required < most && given < required)) {
				takes = "at least " + counted(required, "argument");
			} else if (required < most) {
				takes = "at most " + counted(most, "argument");
			} else {
				takes = most == 0 ? "no arguments" : counted(most, "argument");
			}
			error(p_offset, quoted(p_name) + " takes " + takes + " but " + std::to_string(given) +
									(given == 1 ? " was" : " were") + " given");
			return;
		}
		for (size_t i = 0; i < given && i < most; i++) {
			store(p_arguments[i], compilation_.type_of(p_function->parameters[i].type, p_element));
		}
	}

	// A class in a declaration file that is tagged [Abstract]: one the engine does not let a program create.
	static bool abstract(const Type *p_instance) {
		Symbol *symbol = p_instance->kind == Type::Instance ? p_instance->symbol : nullptr;
		if (!symbol || symbol->kind != SymbolKind::Class || !symbol->unit || !symbol->unit->is_declaration ||
				!symbol->node || symbol->node->kind != NodeKind::ClassDecl) {
			return false;
		}
		for (const Metadata &tag : static_cast<ClassDecl *>(symbol->node)->metadata) {
			if (tag.name == "Abstract") {
				return true;
			}
		}
		return false;
	}

	// The arguments of `new C(...)` or `super(...)`.
	void construct(const Type *p_instance, const List<Expr *> &p_arguments, uint32_t p_offset) {
		bool vector = p_instance->kind == Type::Vector;
		Symbol *owner = vector ? compilation_.vector_class() : p_instance->symbol;
		if (!owner || owner->kind != SymbolKind::Class) {
			return;
		}
		if (Symbol *constructor = owner->klass->constructor) {
			arguments(static_cast<FunctionDecl *>(constructor->node), owner->name, p_arguments, p_offset,
					vector ? p_instance->element : nullptr);
		} else if (!owner->unit->is_declaration && p_arguments.count > 0) {
			// A class that declares no constructor takes no arguments. One in a declaration file may
			// simply not say.
			error(p_offset, quoted(owner->name) + " takes no arguments but " + std::to_string(p_arguments.count) +
									(p_arguments.count == 1 ? " was" : " were") + " given");
		}
	}

	// Where the name of what is being called or constructed is written.
	static uint32_t name_offset(const Expr *p_callee) {
		return p_callee->kind == NodeKind::Member ? static_cast<const Member *>(p_callee)->name_offset : p_callee->offset;
	}

	const Type *call(Call *p_call) {
		list(p_call->arguments);
		Expr *callee = p_call->callee;
		if (!callee) {
			return unknown_;
		}
		if (callee->kind == NodeKind::SuperExpr) {
			callee->type = unknown_;
			if (class_ && !static_ && class_->klass->base && !compilation_.hierarchy_broken(class_)) {
				construct(compilation_.instance_type(class_->klass->base), p_call->arguments, callee->offset);
			}
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
			FunctionDecl *function = static_cast<FunctionDecl *>(symbol->node);
			arguments(function, symbol->name, p_call->arguments, name_offset(callee), element);
			return compilation_.type_of(function->return_type, element);
		}
		switch (type->kind) {
			case Type::ClassObject:
				return type->element; // `int(x)`, `Player(x)`: a conversion.
			case Type::Any:
				return any_;
			case Type::Instance:
				return dynamic_value(type) ? any_ : unknown_;
			default:
				return unknown_;
		}
	}

	// A value that may be a function or a class, known only when the program runs.
	bool dynamic_value(const Type *p_type) const {
		return p_type == function_ || p_type == object_ || p_type == class_type_;
	}

	// ---- operators and indexing that a declaration describes ----
	//
	// A class in a declaration file may carry [Operator(op, right or left, returns)] and [Indexed(element,
	// keyed)] tags. Godot's value types do: its API description lists every operator. The tags are read once
	// per class.

	struct OperatorRule {
		std::string_view op;
		enum Side : uint8_t { Right, Left, Unary } side = Right; // Where the other operand is.
		const Type *other = nullptr; // Null for a unary operator.
		const Type *result = nullptr;
	};
	struct Described {
		std::vector<OperatorRule> operators;
		const Type *element = nullptr; // What indexing gives; null if the class is not indexed.
		bool keyed = false; // Indexed by any key, not only by a number.
		bool complete = false; // The operators listed are all it has: another use is an error.
	};
	std::unordered_map<Symbol *, Described> described_;

	// The type a tag names: `*`, or a class or interface in the tagged class's package, in the top-level
	// package, or written with its package. An unknown name is reported where the tag is.
	const Type *tag_type(Symbol *p_class, const Metadata &p_tag, std::string_view p_name) {
		if (p_name == "*") {
			return any_;
		}
		Package *package = p_class->package;
		std::string_view name = p_name;
		size_t dot = p_name.rfind('.');
		if (dot != std::string_view::npos) {
			package = compilation_.find_package(std::string(p_name.substr(0, dot)));
			name = p_name.substr(dot + 1);
		}
		for (Package *place : { package, dot == std::string_view::npos ? compilation_.top_level() : nullptr }) {
			if (!place) {
				continue;
			}
			auto found = place->definitions.find(name);
			if (found != place->definitions.end() &&
					(found->second->kind == SymbolKind::Class || found->second->kind == SymbolKind::Interface)) {
				return compilation_.instance_type(found->second);
			}
		}
		p_class->unit->diagnostics.error(p_tag.offset, "unknown type " + quoted(p_name) + " in an [" +
															   std::string(p_tag.name) + "] tag");
		return nullptr;
	}

	// What the class's declaration says about operators and indexing, or null if it says nothing.
	const Described *described(const Type *p_type) {
		Symbol *symbol = p_type->kind == Type::Instance ? p_type->symbol : nullptr;
		if (!symbol || symbol->kind != SymbolKind::Class || !symbol->unit || !symbol->unit->is_declaration ||
				!symbol->node || symbol->node->kind != NodeKind::ClassDecl) {
			return nullptr;
		}
		auto cached = described_.find(symbol);
		if (cached != described_.end()) {
			return cached->second.complete || cached->second.element ? &cached->second : nullptr;
		}
		Described result;
		// A subclass is one of its base, so it has what its base has.
		if (symbol->klass && symbol->klass->base) {
			if (const Described *base = described(compilation_.instance_type(symbol->klass->base))) {
				result = *base;
			}
		}
		for (const Metadata &tag : static_cast<ClassDecl *>(symbol->node)->metadata) {
			std::string_view op, left, right, returns, element, keyed;
			for (const MetadataArg &argument : tag.args) {
				if (argument.key == "op") {
					op = argument.value;
				} else if (argument.key == "left") {
					left = argument.value;
				} else if (argument.key == "right") {
					right = argument.value;
				} else if (argument.key == "returns") {
					returns = argument.value;
				} else if (argument.key == "element") {
					element = argument.value;
				} else if (argument.key == "keyed") {
					keyed = argument.value;
				}
			}
			if (tag.name == "Indexed") {
				if (const Type *type = tag_type(symbol, tag, element)) {
					result.element = type;
					result.keyed = keyed == "true";
				}
			} else if (tag.name == "Operators") {
				result.complete = true;
			} else if (tag.name == "Operator") {
				result.complete = true;
				OperatorRule rule;
				rule.op = op;
				rule.result = tag_type(symbol, tag, returns);
				if (!left.empty()) {
					rule.side = OperatorRule::Left;
					rule.other = tag_type(symbol, tag, left);
				} else if (!right.empty()) {
					rule.other = tag_type(symbol, tag, right);
				} else {
					rule.side = OperatorRule::Unary;
				}
				// A tag that names a type the program does not have describes nothing.
				if (!op.empty() && rule.result && (rule.other || rule.side == OperatorRule::Unary)) {
					result.operators.push_back(rule);
				}
			}
		}
		const Described &stored = described_.emplace(symbol, std::move(result)).first->second;
		return stored.complete || stored.element ? &stored : nullptr;
	}

	// Whether the class's declaration lists its operators, so that one it does not list is an error.
	bool has_operators(const Type *p_type) {
		const Described *found = described(p_type);
		return found && found->complete;
	}

	// An expression whose value is a whole number: an int or uint, or sums, differences, products and
	// remainders of such. AS3 computes `i + 1` as a Number, but Godot gives Vector2i * int a Vector2i and
	// Vector2i * float a Vector2, and `cell * (i + 1)` must stay on the grid.
	bool whole(const Expr *p_expression) const {
		if (!p_expression || !p_expression->type) {
			return false;
		}
		if (p_expression->kind == NodeKind::Binary) {
			const Binary *binary = static_cast<const Binary *>(p_expression);
			switch (binary->op) {
				case TokenKind::Plus:
				case TokenKind::Minus:
				case TokenKind::Star:
				case TokenKind::Percent:
					return whole(binary->left) && whole(binary->right);
				default:
					break;
			}
		}
		if (p_expression->kind == NodeKind::Unary) {
			const Unary *unary = static_cast<const Unary *>(p_expression);
			if (unary->op == TokenKind::Minus || unary->op == TokenKind::Plus) {
				return whole(unary->operand);
			}
		}
		return p_expression->type == int_ || p_expression->type == uint_;
	}

	// Whether an operand of type p_type is what a rule asks for. *r_exact is cleared when a whole number
	// meets a rule for Number, or the reverse: such a rule is used only if no other matches.
	bool operand_matches(const Expr *p_operand, const Type *p_type, const Type *p_wanted, bool *r_exact) const {
		*r_exact = true;
		if (numeric(p_wanted)) {
			if (!numeric(p_type)) {
				return false;
			}
			// A whole number is also a Number, so it may use a form declared for Number when there is no
			// other. A fraction is never a whole number.
			bool is_whole = whole(p_operand);
			*r_exact = is_whole == (p_wanted != number_);
			return *r_exact || is_whole;
		}
		if (p_wanted == boolean_ || p_wanted == string_ || numeric(p_type) || p_type == boolean_ || p_type == string_) {
			return p_type == p_wanted; // Everything converts to these two, and they to nothing here.
		}
		return fits(p_type, p_wanted) == Fit::Yes;
	}

	// An operand that could be anything: nothing is said about an operator on it.
	bool open_ended(const Type *p_type) const {
		return p_type->kind == Type::Any || p_type->kind == Type::Unknown || p_type->kind == Type::Null ||
				p_type == object_;
	}

	// The type of `left op right` where a declaration describes operators for one of the operands. Null
	// if neither class describes any: the caller then does what it did before. Reports an operator that
	// the classes don't have.
	const Type *described_binary(TokenKind p_op, Expr *p_left, Expr *p_right, uint32_t p_offset) {
		const Type *left = p_left ? p_left->type : nullptr;
		const Type *right = p_right ? p_right->type : nullptr;
		if (!left || !right || (!has_operators(left) && !has_operators(right))) {
			return nullptr;
		}
		if (open_ended(left) || open_ended(right)) {
			return unknown_;
		}
		std::string_view op = token_kind_spelling(p_op);
		const Type *loose = nullptr;
		for (int pass = 0; pass < 2; pass++) {
			const Described *rules = described(pass == 0 ? left : right);
			if (!rules) {
				continue;
			}
			for (const OperatorRule &rule : rules->operators) {
				if (rule.op != op || rule.side != (pass == 0 ? OperatorRule::Right : OperatorRule::Left)) {
					continue;
				}
				bool exact = true;
				if (!operand_matches(pass == 0 ? p_right : p_left, pass == 0 ? right : left, rule.other, &exact)) {
					continue;
				}
				if (exact) {
					return rule.result;
				}
				if (!loose) {
					loose = rule.result;
				}
			}
		}
		if (loose) {
			return loose;
		}
		error(p_offset, "can't use " + quoted(op) + " on " + quoted(compilation_.type_name(left)) + " and " +
								quoted(compilation_.type_name(right)));
		return unknown_;
	}

	// The same for `-x` and `+x`.
	const Type *described_unary(TokenKind p_op, const Type *p_operand, uint32_t p_offset) {
		const Described *rules = has_operators(p_operand) ? described(p_operand) : nullptr;
		if (!rules) {
			return nullptr;
		}
		std::string_view op = token_kind_spelling(p_op);
		for (const OperatorRule &rule : rules->operators) {
			if (rule.side == OperatorRule::Unary && rule.op == op) {
				return rule.result;
			}
		}
		error(p_offset, "can't use " + quoted(op) + " on " + quoted(compilation_.type_name(p_operand)));
		return unknown_;
	}

	// For the operators no class describes (`++`, `--`, `~`, and the bit assignments): a type whose
	// operators are all known has none of them. True if that was reported.
	bool never_operator(TokenKind p_op, const Type *p_operand, uint32_t p_offset) {
		if (!has_operators(p_operand)) {
			return false;
		}
		error(p_offset, "can't use " + quoted(token_kind_spelling(p_op)) + " on " + quoted(compilation_.type_name(p_operand)));
		return true;
	}

	// The plain operator of a compound assignment, or Error if it has none that a class could describe.
	static TokenKind compound_operator(TokenKind p_op) {
		switch (p_op) {
			case TokenKind::PlusAssign:
				return TokenKind::Plus;
			case TokenKind::MinusAssign:
				return TokenKind::Minus;
			case TokenKind::StarAssign:
				return TokenKind::Star;
			case TokenKind::SlashAssign:
				return TokenKind::Slash;
			case TokenKind::PercentAssign:
				return TokenKind::Percent;
			default:
				return TokenKind::Error;
		}
	}

	const Type *unary(Unary *p_unary) {
		const Type *operand = expression(p_unary->operand);
		if (p_unary->op == TokenKind::Minus || p_unary->op == TokenKind::Plus) {
			if (const Type *result = described_unary(p_unary->op, operand, p_unary->offset)) {
				return result;
			}
		}
		switch (p_unary->op) {
			case TokenKind::Bang:
			case TokenKind::KwDelete:
				return boolean_;
			case TokenKind::KwTypeof:
				return string_;
			case TokenKind::KwVoid:
				return any_;
			case TokenKind::Tilde:
				if (never_operator(p_unary->op, operand, p_unary->offset)) {
					return unknown_;
				}
				return primitive(operand) ? int_ : unknown_;
			case TokenKind::PlusPlus:
			case TokenKind::MinusMinus:
				if (never_operator(p_unary->op, operand, p_unary->offset)) {
					return unknown_;
				}
				written(p_unary->operand);
				return numeric(operand) ? operand : (primitive(operand) ? number_ : unknown_);
			case TokenKind::Minus:
				if (p_unary->operand && p_unary->operand->kind == NodeKind::NumberLit) {
					// `-1` is an int, and so is -2147483648, whose digits alone are not.
					// Not -0: an int has no negative zero.
					unsigned long long value = 0;
					if (whole_value(static_cast<NumberLit *>(p_unary->operand), &value) && value > 0 &&
							value <= 2147483648ull) {
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
		// A string plus anything is a string: the other side is converted.
		if ((p_op == TokenKind::Plus || p_op == TokenKind::PlusAssign) && (p_left == string_ || p_right == string_)) {
			return string_;
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
			case TokenKind::AmpAssign:
			case TokenKind::PipeAssign:
			case TokenKind::CaretAssign:
			case TokenKind::ShiftLeftAssign:
			case TokenKind::ShiftRightAssign:
				return int_;
			case TokenKind::ShiftRightUnsigned:
			case TokenKind::ShiftRightUnsignedAssign:
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
			case TokenKind::Less:
			case TokenKind::Greater:
			case TokenKind::LessEqual:
			case TokenKind::GreaterEqual:
				described_binary(p_binary->op, p_binary->left, p_binary->right, p_binary->offset);
				return boolean_;
			case TokenKind::Equal:
			case TokenKind::NotEqual:
			case TokenKind::StrictEqual:
			case TokenKind::StrictNotEqual:
			case TokenKind::KwIs:
			case TokenKind::KwIn:
			case TokenKind::KwInstanceof:
				return boolean_;
			case TokenKind::KwAs:
				if (right->kind == Type::ClassObject) {
					// The type named, as in Adobe's compiler: `x as Number` may be stored in a Number.
					// (When the value is not one, the null it gives becomes 0 on being stored.)
					return right->element;
				}
				return right->kind == Type::Unknown ? unknown_ : any_;
			case TokenKind::AmpAmp:
			case TokenKind::PipePipe:
				return join(left, right);
			case TokenKind::Comma:
				return right;
			default:
				// A string plus anything is a string, whatever the other side is.
				if (!(p_binary->op == TokenKind::Plus && (left == string_ || right == string_))) {
					if (const Type *result = described_binary(p_binary->op, p_binary->left, p_binary->right,
								p_binary->offset)) {
						return result;
					}
				}
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
					// A definition named by its package. The path below it is not a value.
					for (Expr *part = member->object; part; part = part->kind == NodeKind::Member ? static_cast<Member *>(part)->object : nullptr) {
						part->type = unknown_;
					}
					return symbol_type(member->symbol, nullptr);
				}
				return member_type(expression(member->object), member);
			}
			case NodeKind::Index: {
				Index *index = static_cast<Index *>(p_expression);
				const Type *object = expression(index->object);
				const Type *key = expression(index->index);
				if (object->kind == Type::Vector) {
					// Only a number reaches the elements; `v["length"]` is a property.
					return numeric(key) ? object->element : (key->kind == Type::Unknown ? unknown_ : any_);
				}
				if (object->kind == Type::Unknown || object->kind == Type::Void || object->kind == Type::Null) {
					return unknown_;
				}
				if (const Described *rules = described(object)) {
					// A number reaches the elements. Any other key may name a property, as in `v["x"]`.
					if (rules->element && (rules->keyed || numeric(key))) {
						return rules->element;
					}
				}
				return any_;
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
					if (abstract(callee->element)) {
						error(name_offset(create->callee),
								quoted(callee->element->symbol->name) + " can't be created with 'new'");
						return callee->element;
					}
					construct(callee->element, create->arguments, name_offset(create->callee));
					return callee->element;
				}
				return callee->kind == Type::Any || dynamic_value(callee) ? any_ : unknown_;
			}
			case NodeKind::Unary:
				return unary(static_cast<Unary *>(p_expression));
			case NodeKind::Postfix: {
				const Type *operand = expression(static_cast<Postfix *>(p_expression)->operand);
				if (never_operator(static_cast<Postfix *>(p_expression)->op, operand, p_expression->offset)) {
					return unknown_;
				}
				written(static_cast<Postfix *>(p_expression)->operand);
				return numeric(operand) ? operand : (primitive(operand) ? number_ : unknown_);
			}
			case NodeKind::Binary:
				return binary(static_cast<Binary *>(p_expression));
			case NodeKind::Assign: {
				Assign *assign = static_cast<Assign *>(p_expression);
				const Type *target = expression(assign->target);
				const Type *value = expression(assign->value);
				written(assign->target);
				if (assign->op == TokenKind::Assign) {
					store(assign->value, target);
				}
				if (target->kind == Type::Unknown) {
					return unknown_;
				}
				bool bitwise = false;
				switch (assign->op) {
					case TokenKind::AmpAssign:
					case TokenKind::PipeAssign:
					case TokenKind::CaretAssign:
					case TokenKind::ShiftLeftAssign:
					case TokenKind::ShiftRightAssign:
					case TokenKind::ShiftRightUnsignedAssign:
						bitwise = true;
						break;
					default:
						break;
				}
				if (bitwise && never_operator(assign->op, target, assign->offset)) {
					return target;
				}
				TokenKind plain = compound_operator(assign->op);
				if (plain != TokenKind::Error && !(plain == TokenKind::Plus && (target == string_ || value == string_))) {
					if (const Type *result = described_binary(plain, assign->target, assign->value, assign->offset)) {
						// Value types don't convert to each other, so a result of another type can't go back.
						if (result->kind != Type::Unknown && target->kind != Type::Any &&
								fits(result, target) != Fit::Yes) {
							error(assign->offset, "can't use a value of type " + quoted(compilation_.type_name(result)) +
														  " where " + quoted(compilation_.type_name(target)) +
														  " is expected");
						}
						return target;
					}
				}
				if (assign->op == TokenKind::Assign || (numeric(target) && !bitwise)) {
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
			store(declarator.initializer, compilation_.type_of(declarator.type));
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
				returned(static_cast<Return *>(p_statement));
				break;
			case NodeKind::Throw:
				expression(static_cast<Throw *>(p_statement)->value);
				leaves_ = true;
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

	void returned(Return *p_return) {
		const Type *value = expression(p_return->value);
		if (!p_return->value || !current_) {
			return;
		}
		leaves_ = true;
		bool constructor = class_ && class_->klass->constructor && class_->klass->constructor->node == current_;
		TypeRef *declared = current_->return_type;
		if (constructor) {
			error(p_return->value->offset, "a constructor can't return a value");
		} else if (declared && declared->form == TypeRef::Void) {
			// Passing on the result of another function that returns nothing is left alone, and so is a
			// value that may be `undefined`.
			if (value->kind != Type::Void && value->kind != Type::Unknown && value->kind != Type::Any) {
				error(p_return->value->offset, "a function declared 'void' can't return a value");
			}
		} else {
			store(p_return->value, compilation_.type_of(declared));
		}
	}

	void function(FunctionDecl *p_function) {
		for (Parameter &parameter : p_function->parameters) {
			expression(parameter.default_value);
			if (!parameter.is_rest) {
				store(parameter.default_value, compilation_.type_of(parameter.type));
			}
		}
		FunctionDecl *outer = current_;
		bool outer_leaves = leaves_;
		current_ = p_function;
		leaves_ = false;
		statement(p_function->body);
		// Paths are not followed: only a body with no `return <value>` and no `throw` anywhere is reported.
		TypeRef *declared = p_function->return_type;
		bool constructor = class_ && class_->klass->constructor && class_->klass->constructor->node == p_function;
		if (p_function->body && !leaves_ && !constructor && declared && declared->form != TypeRef::Void &&
				declared->form != TypeRef::Any) {
			const Type *type = compilation_.type_of(declared);
			if (type->kind != Type::Unknown) {
				bool named = !p_function->name.empty();
				error(named ? p_function->name_offset : p_function->offset,
						(named ? quoted(p_function->name) : std::string("this function")) + " is declared to return " +
								quoted(compilation_.type_name(type)) + " but never returns a value");
			}
		}
		current_ = outer;
		leaves_ = outer_leaves;
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
		class_type_ = builtin("Class");
		godot_ = compilation_.find_package("godot");
		if (Package *utils = compilation_.find_package("flash.utils")) {
			auto found = utils->definitions.find("Dictionary");
			if (found != utils->definitions.end()) {
				dictionary_ = compilation_.instance_type(found->second);
			}
		}
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
