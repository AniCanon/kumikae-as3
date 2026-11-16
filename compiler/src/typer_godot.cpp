#include "typer.h"

#include <algorithm>

namespace kas3 {

// A mistake in a class's tag is said when the class's own file is checked, once: every file that uses the
// class reads the tags again, and says nothing.
void Typer::tag_error(const Symbol *p_class, const Metadata &p_tag, std::string p_message) {
	if (p_class->unit == &unit_) {
		unit_.diagnostics.error(p_tag.offset, std::move(p_message));
	}
}

const Type *Typer::tag_type(Symbol *p_class, const Metadata &p_tag, std::string_view p_name) {
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
		// A declaration file describes what was compiled without this program: a class of the program by
		// the name is not what its tag means (and the file is checked once, whatever the program defines later).
		auto found = place->definitions.find(name);
		if (found != place->definitions.end() && found->second->unit && found->second->unit->is_declaration &&
				(found->second->kind == SymbolKind::Class || found->second->kind == SymbolKind::Interface)) {
			return compilation_.instance_type(found->second);
		}
	}
	tag_error(p_class, p_tag, "unknown type " + quoted(p_name) + " in an [" + std::string(p_tag.name) + "] tag");
	return nullptr;
}

// The named arguments of a tag that describes a class's operators or indexing.
Typer::TagArguments Typer::tag_arguments(const Metadata &p_tag) {
	TagArguments result;
	for (const MetadataArg &argument : p_tag.args) {
		if (argument.key == "op") {
			result.op = argument.value;
		} else if (argument.key == "left") {
			result.left = argument.value;
		} else if (argument.key == "right") {
			result.right = argument.value;
		} else if (argument.key == "returns") {
			result.returns = argument.value;
		} else if (argument.key == "element") {
			result.element = argument.value;
		} else if (argument.key == "keyed") {
			result.keyed = argument.value;
		}
	}
	return result;
}

// A [Constructor(args="A,B")] tag: one form of the constructor, kept when every type it names is known.
void Typer::constructor_tag(Symbol *p_class, const Metadata &p_tag, Described &r_result) {
	std::vector<const Type *> form;
	std::string_view args;
	for (const MetadataArg &argument : p_tag.args) {
		args = argument.key == "args" ? argument.value : args;
	}
	bool known = true;
	for (size_t start = 0; !args.empty() && start <= args.size();) {
		size_t comma = args.find(',', start);
		const Type *type = tag_type(p_class, p_tag, args.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
		known = known && type;
		form.push_back(type);
		start = comma == std::string_view::npos ? args.size() + 1 : comma + 1;
	}
	if (known) {
		r_result.forms.push_back(std::move(form));
	}
}

// An [Operator(op, left or right, returns)] tag. p_inherited operators came from the base, which a class may
// repeat.
void Typer::operator_tag(Symbol *p_class, const Metadata &p_tag, const TagArguments &p_arguments, size_t p_inherited,
		Described &r_result) {
	const auto &[op, left, right, returns, element, keyed] = p_arguments;
	r_result.complete = true;
	if (!left.empty() && !right.empty()) {
		tag_error(p_class, p_tag, "an [Operator] tag names both a left and a right operand");
		return;
	}
	OperatorRule rule;
	rule.op = op;
	rule.result = tag_type(p_class, p_tag, returns);
	if (!left.empty()) {
		rule.side = OperatorRule::Left;
		rule.other = tag_type(p_class, p_tag, left);
	} else if (!right.empty()) {
		rule.other = tag_type(p_class, p_tag, right);
	} else {
		rule.side = OperatorRule::Unary;
	}
	// A tag that names a type the program does not have describes nothing.
	if (!op.empty() && rule.result && (rule.other || rule.side == OperatorRule::Unary)) {
		const auto same = [&](const OperatorRule &p_other) {
			return p_other.op == rule.op && p_other.side == rule.side && p_other.other == rule.other;
		};
		if (std::any_of(r_result.operators.begin() + static_cast<std::ptrdiff_t>(p_inherited), r_result.operators.end(), same)) {
			const std::string where = rule.side == OperatorRule::Unary
					? std::string(" with no other operand")
					: " with " + quoted(compilation_.type_name(rule.other)) + (rule.side == OperatorRule::Left ? " on the left" : " on the right");
			tag_error(p_class, p_tag, "a second [Operator] tag for " + quoted(op) + where);
			return;
		}
		r_result.operators.push_back(rule);
	}
}

auto Typer::described(const Type *p_type) -> const Described * {
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
	const size_t inherited = result.operators.size(); // A class may say again what its base says.
	for (const Metadata &tag : static_cast<ClassDecl *>(symbol->node)->metadata) {
		const TagArguments arguments = tag_arguments(tag);
		if (tag.name == "Indexed") {
			if (const Type *type = tag_type(symbol, tag, arguments.element)) {
				result.element = type;
				result.keyed = arguments.keyed == "true";
			}
		} else if (tag.name == "Constructor") {
			constructor_tag(symbol, tag, result);
		} else if (tag.name == "Operators") {
			result.complete = true;
		} else if (tag.name == "Operator") {
			operator_tag(symbol, tag, arguments, inherited, result);
		}
	}
	const Described &stored = described_.emplace(symbol, std::move(result)).first->second;
	return stored.complete || stored.element ? &stored : nullptr;
}

bool Typer::has_operators(const Type *p_type) {
	const Described *found = described(p_type);
	return found && found->complete;
}

bool Typer::whole(const Expr *p_expression) const {
	return is_whole_number(p_expression, [this](const Expr *p_operand) { return integral(p_operand->type); });
}

bool Typer::operand_matches(const Expr *p_operand, const Type *p_type, const Type *p_wanted, bool *r_exact) const {
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

bool Typer::open_ended(const Type *p_type) const {
	return p_type->kind == Type::Any || p_type->kind == Type::Unknown || p_type->kind == Type::Null ||
			p_type == object_;
}

const Type *Typer::described_binary(TokenKind p_op, Expr *p_left, Expr *p_right, uint32_t p_offset) {
	const Type *left = p_left ? p_left->type : nullptr;
	const Type *right = p_right ? p_right->type : nullptr;
	if (!left || !right || (!has_operators(left) && !has_operators(right))) {
		return nullptr;
	}
	if (left->kind == Type::Unknown || right->kind == Type::Unknown) {
		return unknown_;
	}
	if (open_ended(left) || open_ended(right)) {
		return any_; // An untyped side: Godot works it out when the program runs.
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

void Typer::constructor_form(const Type *p_instance, const List<Expr *> &p_arguments, uint32_t p_offset) {
	const Described *rules = described(p_instance);
	if (!rules || rules->forms.empty()) {
		return;
	}
	std::vector<Expr *> arguments(p_arguments.begin(), p_arguments.end());
	for (Expr *argument : arguments) {
		if (!argument->type || argument->type->kind == Type::Unknown) {
			return; // Already reported.
		}
	}
	for (const std::vector<const Type *> &form : rules->forms) {
		bool takes = form.size() == arguments.size();
		for (size_t i = 0; takes && i < form.size(); i++) {
			bool exact = true;
			takes = open_ended(arguments[i]->type) || operand_matches(arguments[i], arguments[i]->type, form[i], &exact);
		}
		if (takes) {
			return;
		}
	}
	std::string given;
	for (Expr *argument : arguments) {
		given += (given.empty() ? "" : ", ") + quoted(compilation_.type_name(argument->type));
	}
	error(p_offset, "no constructor of " + quoted(compilation_.type_name(p_instance)) + " takes " +
							(given.empty() ? std::string("no arguments") : given));
}

const Type *Typer::described_unary(TokenKind p_op, const Type *p_operand, uint32_t p_offset) {
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

bool Typer::never_operator(TokenKind p_op, const Type *p_operand, uint32_t p_offset) {
	if (!has_operators(p_operand)) {
		return false;
	}
	error(p_offset, "can't use " + quoted(token_kind_spelling(p_op)) + " on " + quoted(compilation_.type_name(p_operand)));
	return true;
}

TokenKind Typer::compound_operator(TokenKind p_op) {
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

// `[Signal] public var hit:Signal;`: an instance field of type Signal, with no value written, in a class that
// extends one of Godot's. Godot is told of the signal; the field holds it from the start.
void Typer::signal_field(VarDecl *p_decl) {
	const Metadata *tag = nullptr;
	for (const Metadata &each : p_decl->metadata) {
		tag = each.name == "Signal" ? &each : tag;
	}
	if (!tag) {
		return;
	}
	if (tag->args.count) {
		error(tag->offset, "[Signal] takes nothing: a signal is given whatever emit() is given");
	}
	if (p_decl->modifiers.bits & Modifiers::Static) {
		error(tag->offset, "a [Signal] field can't be static: a signal belongs to an object");
	}
	const Symbol *object = nullptr;
	if (godot_) {
		const auto found = godot_->definitions.find("GodotObject");
		object = found == godot_->definitions.end() ? nullptr : found->second;
	}
	bool godot_class = false;
	for (const Symbol *at = class_; at && at->klass && object; at = at->klass->base) {
		godot_class = godot_class || at == object;
	}
	if (!godot_class) {
		error(tag->offset, "[Signal] is for a class that extends one of Godot's: only a Godot object has signals");
	}
	for (Declarator &declarator : p_decl->declarators) {
		const Type *type = declarator.type ? compilation_.type_of(declarator.type) : nullptr;
		const bool signal = type && type->kind == Type::Instance && type->symbol && type->symbol->name == "Signal" && godot_ &&
				godot_->definitions.count("Signal") && godot_->definitions.find("Signal")->second == type->symbol;
		if (!signal && type && type->kind != Type::Unknown) {
			error(declarator.offset, quoted(declarator.name) + " is a [Signal] field, so its type is 'Signal'");
		}
		if (declarator.initializer) {
			error(declarator.initializer->offset, "a [Signal] field holds its own signal: no value is written for it");
		}
	}
}

// Whether Godot can store a value of the type in a scene and show it in the inspector: a number, a Boolean,
// a String, an Array, one of Godot's values (but a Signal or an RID, which belong to the running engine),
// a Resource, or a Node when the script is a node's.
bool Typer::godot_storable(const Type *p_type, bool p_in_node) const {
	if (numeric(p_type) || p_type == boolean_ || p_type == string_ || p_type == array_) {
		return true;
	}
	if (!godot_ || !p_type || p_type->kind != Type::Instance || !p_type->symbol || !p_type->symbol->klass) {
		return false;
	}
	const auto godot_class = [&](std::string_view p_name) {
		const auto found = godot_->definitions.find(p_name);
		return found == godot_->definitions.end() ? nullptr : found->second;
	};
	Symbol *symbol = p_type->symbol;
	if (godot_class(symbol->name) != symbol) {
		return false; // Not one of Godot's.
	}
	const auto extends = [&](Symbol *p_base) {
		for (Symbol *at = symbol; at && at->klass && p_base; at = at->klass->base) {
			if (at == p_base) {
				return true;
			}
		}
		return false;
	};
	if (!extends(godot_class("GodotObject"))) {
		return symbol->name != "Signal" && symbol->name != "RID";
	}
	return extends(godot_class("Resource")) || (p_in_node && extends(godot_class("Node")));
}

// One of Godot's values a scene can store (a Vector2, a Color, a GodotArray): storable, and not an object.
bool Typer::godot_value(const Type *p_type) const {
	if (!godot_storable(p_type, true) || numeric(p_type) || p_type == boolean_ || p_type == string_ || p_type == array_) {
		return false;
	}
	const auto object = godot_->definitions.find("GodotObject");
	for (const Symbol *at = p_type->symbol; at && at->klass; at = at->klass->base) {
		if (object != godot_->definitions.end() && at == object->second) {
			return false;
		}
	}
	return true;
}

// `[Export] public var speed:Number = 5;`: a public instance field, not a const, of a type Godot can store,
// in a class that extends one of Godot's. Godot's inspector shows it, and a scene's value is given to it.
void Typer::export_field(VarDecl *p_decl) {
	const Metadata *tag = nullptr;
	for (const Metadata &each : p_decl->metadata) {
		tag = each.name == "Export" ? &each : tag;
	}
	if (!tag) {
		return;
	}
	if (tag->args.count) {
		error(tag->offset, "[Export] takes nothing: the field's type says what the inspector shows");
	}
	if (p_decl->modifiers.bits & Modifiers::Static) {
		error(tag->offset, "an [Export] field can't be static: a scene keeps a value for each object");
	}
	if (!(p_decl->modifiers.bits & Modifiers::Public)) {
		error(tag->offset, "an [Export] field is public: Godot's inspector writes it from outside the class");
	}
	if (p_decl->is_const) {
		error(tag->offset, "an [Export] field can't be a const: the scene's value is written to it");
	}
	Symbol *object = nullptr;
	Symbol *node = nullptr;
	if (godot_) {
		const auto found = godot_->definitions.find("GodotObject");
		object = found == godot_->definitions.end() ? nullptr : found->second;
		const auto found_node = godot_->definitions.find("Node");
		node = found_node == godot_->definitions.end() ? nullptr : found_node->second;
	}
	bool godot_class = false;
	bool in_node = false;
	for (const Symbol *at = class_; at && at->klass; at = at->klass->base) {
		godot_class = godot_class || (object && at == object);
		in_node = in_node || (node && at == node);
	}
	if (!godot_class) {
		error(tag->offset, "[Export] is for a class that extends one of Godot's: only a Godot object is kept in a scene");
		return;
	}
	for (Declarator &declarator : p_decl->declarators) {
		const Type *type = declarator.type ? compilation_.type_of(declarator.type) : any_;
		if ((!type || type->kind != Type::Unknown) && !godot_storable(type, in_node)) {
			error(declarator.offset, quoted(declarator.name) + " is an [Export] field, so its type is one Godot can store: a number, " +
					"Boolean, String, Array, one of Godot's values, a Resource, or a Node in a node's script");
		}
		// Godot's inspector is told the first value, so it is a constant: `new` makes one only of Godot's values.
		const Expr *first = declarator.initializer;
		if (!first && type == number_) {
			error(declarator.offset, quoted(declarator.name) + " is an [Export] Number, so its first value is written: a Number " +
					"starts as NaN, which Godot's inspector can't show");
		}
		if (first && (!export_constant(first) || (first->kind == NodeKind::New && !godot_value(first->type)))) {
			error(first->offset, std::string("an [Export] field's first value is a constant, which Godot's inspector is told: a ") +
					"number, String, Boolean, null, or one of Godot's values made from numbers");
		} else if (first && ((type == boolean_ && first->kind != NodeKind::BoolLit) || (numeric(type) && first->kind == NodeKind::NullLit))) {
			// Anything fits a Boolean and null fits a number, but Godot is told the constant as it is written.
			error(first->offset, quoted(declarator.name) + " is an [Export] field, so its first value is written as its type: " +
					(type == boolean_ ? "true or false" : "a number"));
		}
	}
}

} // namespace kas3
