// Function expressions and the variables they capture (docs: the function values design).
//
// A local or a parameter that a function written inside its function reads or writes lives in an
// environment: an object made as the function starts, one field for each such variable, its first field
// the environment the function itself was given. Every read and write of the variable, in the function
// and in those written inside it, goes through that object, so that they all see one variable.
#include "lower_bodies.h"
#include "lower_engine_values.h"
#include "lower_functions.h"
#include "lower_library_values.h"
#include "lower_statements.h"

namespace kas3 {
namespace ir {

namespace {

bool nested_function(const Node *p_node) {
	return p_node->kind == NodeKind::FunctionExpr || p_node->kind == NodeKind::FunctionStmt;
}

// What the functions written inside a body use, at any depth; and whether they name a member of the object.
std::unordered_set<const Symbol *> used_inside(Node *p_body, bool *r_members) {
	std::unordered_set<const Symbol *> used;
	bool members = false;
	walk(p_body, [&](Node *p_node) -> bool {
		if (nested_function(p_node)) {
			walk(p_node, [&](Node *p_inner) {
				if (p_inner->kind == NodeKind::Name && static_cast<Name *>(p_inner)->symbol) {
					used.insert(static_cast<Name *>(p_inner)->symbol);
					members = members || static_cast<Name *>(p_inner)->symbol->is_instance_member();
				}
			});
			return false;
		}
		return true;
	});
	*r_members = members;
	return used;
}

} // namespace

// After the function's parameters and locals are declared: the environment, if a function written inside
// it uses any of them.
void Functions::make_environment(FunctionDecl *p_decl) {
	l_.own_env_reg_ = NONE;
	l_.own_layout_ = EnvLayout();
	if (!p_decl || !p_decl->body) {
		return;
	}
	bool members = false;
	const std::unordered_set<const Symbol *> used = used_inside(p_decl->body, &members);
	// A method's: the functions written in it see the object's members, as AS3's scope chain has it.
	const bool keeps_this = members && !l_.fn().is_static && !l_.received_env_;
	std::vector<const Symbol *> captured;
	for (const auto &[symbol, local] : l_.locals_) {
		if (used.count(symbol) && symbol->kind != SymbolKind::CatchVariable) {
			captured.push_back(symbol);
		}
	}
	if (captured.empty() && !keeps_this) {
		return;
	}
	// In the order they are declared, so that the class is the same each time the program is compiled.
	std::sort(captured.begin(), captured.end(), [](const Symbol *p_a, const Symbol *p_b) { return p_a->offset < p_b->offset; });
	Class klass;
	klass.name = l_.fn().name + "$environment";
	klass.fields.push_back({ "$outer", Kind::Obj, l_.chain_.empty() ? NONE : l_.chain_.front().klass });
	if (keeps_this) {
		l_.own_layout_.this_field = static_cast<uint32_t>(klass.fields.size());
		klass.fields.push_back({ "$this", Kind::Obj, l_.fn().owner });
	}
	for (const Symbol *symbol : captured) {
		l_.own_layout_.fields[symbol] = static_cast<uint32_t>(klass.fields.size());
		const uint32_t local = l_.locals_[symbol];
		const Kind kind = l_.kind_at(local);
		const bool declared = symbol->kind == SymbolKind::Local || (symbol->kind == SymbolKind::Parameter && !symbol->is_rest);
		klass.fields.push_back({ symbol->name, kind, declared && kind == Kind::Obj ? l_.declared_class(symbol->type) : NONE });
		if (is_engine(kind) && local < l_.fn().engine_types.size()) {
			klass.fields.back().engine_type = l_.fn().engine_types[local];
		}
	}
	l_.module_.classes.push_back(std::move(klass));
	l_.own_layout_.klass = static_cast<uint32_t>(l_.module_.classes.size()) - 1;
	const uint32_t position = p_decl->offset;
	l_.own_env_reg_ = l_.reg(Kind::Obj);
	l_.emit(Op::New, position, l_.own_env_reg_, l_.own_layout_.klass);
	if (l_.received_env_) {
		l_.emit(Op::SetField, position, l_.own_env_reg_, 0, 0); // The environment it was given, from r0.
	}
	if (keeps_this) {
		l_.emit(Op::SetField, position, l_.own_env_reg_, l_.own_layout_.this_field, 0);
	}
	// What each holds now: a parameter's argument, a local's starting value.
	for (const Symbol *symbol : captured) {
		l_.emit(Op::SetField, position, l_.own_env_reg_, l_.own_layout_.fields[symbol], l_.locals_[symbol]);
	}
}

// `this` is register 0 of a method. In a function expression register 0 is the environment: a member named
// without `this` is of the object the function was written in, kept in an environment; AS3's own `this`
// there is the caller's object, which the interpreter has not got, and is refused.
uint32_t Lowerer::self(uint32_t p_position, bool p_written) {
	if (!received_env_) {
		return 0;
	}
	for (size_t depth = 0; !p_written && depth < chain_.size(); depth++) {
		if (chain_[depth].this_field == NONE) {
			continue;
		}
		uint32_t at = 0;
		for (size_t step = 0; step < depth; step++) {
			const uint32_t outer = reg(Kind::Obj);
			emit(Op::GetField, p_position, outer, at, 0);
			at = outer;
		}
		const uint32_t object = reg(Kind::Obj);
		emit(Op::GetField, p_position, object, at, chain_[depth].this_field);
		return object;
	}
	refuse(p_position, p_written ? "'this' in a function expression" : "an instance member named inside this function expression");
	return default_value(Kind::Obj, p_position);
}

// A variable a function written inside this one shares, or one of an enclosing function's that this one
// uses: its field in an environment. Nothing for any other variable.
std::optional<Lowerer::Place> Functions::captured_place(const Symbol *p_symbol, uint32_t p_position) {
	Place place;
	place.where = Place::FieldOf;
	if (l_.own_env_reg_ != NONE) {
		if (const auto found = l_.own_layout_.fields.find(p_symbol); found != l_.own_layout_.fields.end()) {
			place.object = l_.own_env_reg_;
			place.index = found->second;
			place.kind = l_.module_.classes[l_.own_layout_.klass].fields[found->second].kind;
			return place;
		}
	}
	for (size_t depth = 0; depth < l_.chain_.size(); depth++) {
		const auto found = l_.chain_[depth].fields.find(p_symbol);
		if (found == l_.chain_[depth].fields.end()) {
			continue;
		}
		// The environment this function was given (r0), then each one's outer one.
		uint32_t at = 0;
		for (size_t step = 0; step < depth; step++) {
			const uint32_t outer = l_.reg(Kind::Obj);
			l_.emit(Op::GetField, p_position, outer, at, 0);
			at = outer;
		}
		place.object = at;
		place.index = found->second;
		place.kind = l_.module_.classes[l_.chain_[depth].klass].fields[found->second].kind;
		return place;
	}
	return std::nullopt;
}

// `function (a:int):int { ... }`: a function value of a function of its own, given the environment it may
// use. Lowered once the function it is written in is done.
uint32_t Functions::closure_value(FunctionExpr *p_expression) {
	const uint32_t position = p_expression->offset;
	uint32_t klass = NONE;
	if (!p_expression->function || !l_.class_index(l_.function_, &klass)) {
		l_.refuse(position, "this function expression");
		return l_.default_value(Kind::Obj, position);
	}
	return closure_of(p_expression->function, true, position);
}

uint32_t Functions::closure_of(FunctionDecl *p_decl, bool p_expression, uint32_t p_position) {
	Function made;
	made.name = l_.fn().name + "/" + (p_decl->name.empty() ? std::string("function") : std::string(p_decl->name));
	made.file = l_.fn().file;
	made.position = p_position;
	made.owner = NONE;
	made.written_in = l_.fn().owner != NONE ? l_.fn().owner : l_.fn().written_in;
	made.is_static = false; // Its first register is the environment.
	l_.module_.functions.push_back(std::move(made));
	const uint32_t index = static_cast<uint32_t>(l_.module_.functions.size()) - 1;
	const uint32_t environment = l_.own_env_reg_ != NONE ? l_.own_env_reg_ : l_.received_env_ ? 0 : NONE;
	PendingFunction pending;
	pending.decl = p_decl;
	pending.index = index;
	pending.unit = l_.unit_;
	pending.klass = l_.class_;
	pending.expression = p_expression;
	pending.given_environment = environment != NONE;
	if (l_.own_env_reg_ != NONE) {
		pending.chain.push_back(l_.own_layout_);
	}
	if (l_.received_env_) {
		pending.chain.insert(pending.chain.end(), l_.chain_.begin(), l_.chain_.end());
	}
	l_.pending_functions_.push_back(std::move(pending));
	uint32_t out = l_.reg(Kind::Obj);
	l_.emit(Op::MakeFunction, p_position, out, index, environment);
	return out;
}

// `function helper():void {}` written in a function: a variable of that function, which holds the function
// from the start, so that a line above it can call it, as in AS3.
void Functions::declare_function(FunctionDecl *p_decl) {
	if (!p_decl || !p_decl->symbol || l_.locals_.count(p_decl->symbol)) {
		return;
	}
	const uint32_t local = l_.reg(Kind::Obj);
	l_.locals_[p_decl->symbol] = local;
	l_.fn().locals.push_back({ std::string(p_decl->name), local });
}

// After the environment is made, which the functions are given: each declared function's value, in the
// order they are written.
void Functions::nested_functions(Node *p_body) {
	uint32_t klass = NONE;
	walk(p_body, [&](Node *p_node) -> bool {
		if (p_node->kind == NodeKind::FunctionStmt) {
			FunctionDecl *decl = static_cast<FunctionStmt *>(p_node)->function;
			if (!decl || !decl->symbol || !l_.class_index(l_.function_, &klass)) {
				l_.refuse(p_node->offset, "this function");
				return false;
			}
			const uint32_t position = p_node->offset;
			l_.store(l_.place_of_symbol(decl->symbol, 0, position), closure_of(decl, false, position), position);
		}
		return !nested_function(p_node);
	});
}

// The function expressions met while lowering a function, each as a function of its own; and those met in
// them.
void Functions::lower_pending_functions() {
	while (!l_.pending_functions_.empty()) {
		PendingFunction pending = std::move(l_.pending_functions_.back());
		l_.pending_functions_.pop_back();
		Bodies(l_).begin(pending.index, *pending.unit, pending.klass);
		if (pending.forwarded && pending.written) {
			LibraryValues(l_).lower(pending);
			continue;
		}
		if (pending.forwarded) {
			EngineValues(l_).lower(pending);
			continue;
		}
		l_.received_env_ = true;
		l_.chain_ = std::move(pending.chain);
		FunctionDecl *decl = pending.decl;
		l_.result_type_ = decl->return_type ? l_.compilation_.type_of(decl->return_type) : l_.compilation_.any_type();
		Bodies(l_).parameters(decl);
		if (!l_.chain_.empty()) {
			auto &classes = l_.fn().parameter_classes;
			if (classes.empty()) {
				classes.resize(1);
			}
			classes[0] = l_.chain_.front().klass;
		}
		l_.fn().locals[0].name = "$environment";
		l_.fn().result = decl->return_type ? l_.written_kind(decl->return_type, decl->return_type->offset, Kind::Void) : Kind::Any;
		if (decl->body) {
			Statements(l_).declare_locals(decl->body);
			// A function expression's own name is a variable of its own that holds it: `function f() { f(); }`.
			const bool named = pending.expression && decl->symbol && decl->symbol->kind == SymbolKind::LocalFunction;
			if (named) {
				declare_function(decl);
				l_.emit(Op::MakeFunction, decl->offset, l_.locals_[decl->symbol], pending.index, pending.given_environment ? 0 : NONE);
			}
			make_environment(decl);
			nested_functions(decl->body);
			Statements(l_).statement(decl->body);
		}
		Bodies(l_).end(decl->end ? decl->end - 1 : decl->offset);
	}
}

} // namespace ir
} // namespace kas3
