#include "query.h"

#include "ast_walk.h"

#include <algorithm>

namespace kas3 {

const std::vector<Node *> &Queries::context_nodes(const Unit &p_unit) const {
	auto [entry, inserted] = context_nodes_.try_emplace(&p_unit);
	if (inserted) {
		auto &nodes = entry->second;
		walk(p_unit.tree, [&](Node *p_node) {
			switch (p_node->kind) {
				case NodeKind::TypeRef:
				case NodeKind::ClassDecl:
				case NodeKind::InterfaceDecl:
				case NodeKind::FunctionDecl:
				case NodeKind::Member:
				case NodeKind::ExprStmt:
				case NodeKind::Call:
				case NodeKind::New:
				case NodeKind::Block:
				case NodeKind::If:
				case NodeKind::While:
				case NodeKind::DoWhile:
				case NodeKind::For:
				case NodeKind::ForIn:
				case NodeKind::Switch:
				case NodeKind::Labeled:
					nodes.push_back(p_node);
					break;
				default:
					break;
			}
		});
	}
	return entry->second;
}

const LocalScope *Queries::local_scope_at(const Unit &p_unit, uint32_t p_offset) const {
	const LocalScope *scope = nullptr;
	for (const LocalScope &candidate : p_unit.local_scopes) {
		if (candidate.extent->offset < p_offset && p_offset <= candidate.extent->end &&
				(!scope || candidate.extent->offset >= scope->extent->offset)) {
			scope = &candidate;
		}
	}
	return scope;
}

bool Queries::text_byte(const Unit &p_unit, uint32_t p_offset) const {
	const auto &regions = p_unit.parsed->text_regions;
	auto after = std::upper_bound(regions.begin(), regions.end(), p_offset,
			[](uint32_t p_at, const TextRegion &p_region) { return p_at < p_region.begin; });
	return after != regions.begin() && p_offset < std::prev(after)->end;
}

bool Queries::in_text(const Unit &p_unit, uint32_t p_offset) const {
	const auto &regions = p_unit.parsed->text_regions;
	auto after = std::lower_bound(regions.begin(), regions.end(), p_offset,
			[](const TextRegion &p_region, uint32_t p_at) { return p_region.begin < p_at; });
	if (after == regions.begin()) {
		return false;
	}
	const TextRegion &region = *std::prev(after);
	return p_offset < region.end || (p_offset == region.end && !region.closed);
}

} // namespace kas3
