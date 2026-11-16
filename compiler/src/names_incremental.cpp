#include "names.h"

#include "typer.h"
#include "ast_walk.h"

#include <algorithm>
#include <optional>

namespace kas3 {

void resolve_unit_bodies(Compilation &p_compilation, Unit &p_unit);

namespace {

struct Shape {
	std::vector<Node *> declarations;
	std::vector<const Block *> bodies;
	std::string signature;
};

bool collect(Node *p_node, Shape &r_shape) {
	switch (p_node->kind) {
		case NodeKind::ClassDecl:
			r_shape.declarations.push_back(p_node);
			for (Node *member : static_cast<ClassDecl *>(p_node)->members) {
				if (!collect(member, r_shape)) {
					return false;
				}
			}
			break;
		case NodeKind::InterfaceDecl:
			// Inherited property halves keep pointers to accessor declarations. Until those bindings
			// have stable identities of their own, interface/accessor edits use the full checker.
			return false;
		case NodeKind::FunctionDecl: {
			auto *function = static_cast<FunctionDecl *>(p_node);
			if (function->accessor != FunctionDecl::None) {
				return false;
			}
			r_shape.declarations.push_back(p_node);
			if (function->body) {
				r_shape.bodies.push_back(function->body);
			}
		} break;
		case NodeKind::VarDecl:
			r_shape.declarations.push_back(p_node);
			break;
		default:
			break;
	}
	return true;
}

std::optional<Shape> shape(const Parsed &p_parsed) {
	if (p_parsed.diagnostics.has_errors()) {
		return std::nullopt;
	}
	Shape result;
	if (p_parsed.tree->package) {
		for (Node *node : p_parsed.tree->package->definitions) {
			if (!collect(node, result)) {
				return std::nullopt;
			}
		}
	}
	for (Node *node : p_parsed.tree->definitions) {
		if (!collect(node, result)) {
			return std::nullopt;
		}
	}
	std::sort(result.bodies.begin(), result.bodies.end(), [](const Block *p_a, const Block *p_b) {
		return p_a->offset < p_b->offset;
	});
	uint32_t begin = 0;
	const std::string &source = p_parsed.source.text();
	for (const Block *body : result.bodies) {
		if (body->offset < begin || body->end > source.size()) {
			return std::nullopt;
		}
		result.signature.append(source, begin, body->offset - begin);
		result.signature += "{}";
		begin = body->end;
	}
	result.signature.append(source, begin, std::string::npos);
	// Function imports can resolve public parameter/return types. Moving an import into a nested
	// function changes that scope even if its text stays identical, so these bodies use full checking.
	bool body_import = false;
	walk(p_parsed.tree, [&](Node *p_node) {
		if (p_node->kind != NodeKind::Import) {
			return;
		}
		const auto after = std::upper_bound(result.bodies.begin(), result.bodies.end(), p_node->offset,
				[](uint32_t p_offset, const Block *p_body) { return p_offset < p_body->offset; });
		body_import |= after != result.bodies.begin() && p_node->end <= (*std::prev(after))->end;
	});
	if (body_import) {
		return std::nullopt;
	}
	return result;
}

void bind_symbol(Symbol *p_symbol, Node *p_node, uint32_t p_offset, TypeRef *p_type = nullptr) {
	p_symbol->node = p_node;
	p_symbol->offset = p_offset;
	p_symbol->type = p_type;
}

void rebind(Node *p_before, Node *p_after) {
	switch (p_before->kind) {
		case NodeKind::ClassDecl: {
			auto *before = static_cast<ClassDecl *>(p_before);
			auto *after = static_cast<ClassDecl *>(p_after);
			after->symbol = before->symbol;
			bind_symbol(after->symbol, after, after->name_offset);
			after->symbol->klass->imports = {};
		} break;
		case NodeKind::FunctionDecl: {
			auto *before = static_cast<FunctionDecl *>(p_before);
			auto *after = static_cast<FunctionDecl *>(p_after);
			after->symbol = before->symbol;
			bind_symbol(after->symbol, after, after->name_offset, after->return_type);
		} break;
		case NodeKind::VarDecl: {
			auto *before = static_cast<VarDecl *>(p_before);
			auto *after = static_cast<VarDecl *>(p_after);
			for (uint32_t i = 0; i < before->declarators.count; ++i) {
				auto &declaration = after->declarators[i];
				declaration.symbol = before->declarators[i].symbol;
				bind_symbol(declaration.symbol, after, declaration.offset, declaration.type);
			}
		} break;
		default:
			break;
	}
}

void hierarchy_references(Compilation &p_compilation, Unit &p_unit) {
	auto record = [&](const List<Node *> &p_definitions) {
		for (Node *node : p_definitions) {
			if (node->kind == NodeKind::ClassDecl) {
				auto *decl = static_cast<ClassDecl *>(node);
				const Context context = p_compilation.context_of(decl->symbol);
				p_compilation.resolve_type(context, decl->extends);
				for (TypeRef *type : decl->implements) {
					p_compilation.resolve_type(context, type);
				}
			}
		}
	};
	if (p_unit.tree->package) {
		record(p_unit.tree->package->definitions);
	}
	record(p_unit.tree->definitions);
}

} // namespace

bool Compilation::update_bodies(const std::vector<std::pair<Unit *, std::shared_ptr<Parsed>>> &p_changes) {
	// Rebase the interning tables periodically instead of keeping types introduced by old edits forever.
	constexpr size_t MAX_EXTRA_TYPES = 1024;
	if (!reusable_bodies_ || types_.size() > initial_type_count_ + MAX_EXTRA_TYPES) {
		return false;
	}
	std::vector<std::pair<Shape, Shape>> shapes;
	for (const auto &[unit, parsed] : p_changes) {
		if (unit->is_declaration || parsed->used) {
			return false;
		}
		auto before = shape(*unit->parsed);
		auto after = shape(*parsed);
		if (!before || !after || before->signature != after->signature ||
				before->declarations.size() != after->declarations.size()) {
			return false;
		}
		shapes.emplace_back(std::move(*before), std::move(*after));
	}
	for (size_t i = 0; i < p_changes.size(); ++i) {
		auto &[before, after] = shapes[i];
		for (size_t j = 0; j < before.declarations.size(); ++j) {
			rebind(before.declarations[j], after.declarations[j]);
		}
		auto &[unit, parsed] = p_changes[i];
		unit->references.clear();
		unit->typed.clear();
		unit->local_scopes.clear();
		unit->body_symbols.clear();
		unit->outer_imports = {};
		unit->block_imports = {};
		unit->diagnostics = parsed->diagnostics;
		unit->source = parsed->source;
		unit->tree = parsed->tree;
		unit->parsed = parsed;
		parsed->used = true;
	}
	for (const auto &[unit, parsed] : p_changes) {
		link_imports(*unit);
		hierarchy_references(*this, *unit);
		resolve_unit_bodies(*this, *unit);
	}
	for (const auto &[unit, parsed] : p_changes) {
		Typer(*this, *unit).run();
	}
	return true;
}

} // namespace kas3
