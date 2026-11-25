// Reverse edges from a tentative expression type to the local proofs that read it.
#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kas3 {

struct Expr;

class ProofDependencies {
	std::unordered_map<Expr *, std::unordered_set<size_t>> readers_;
	std::vector<std::vector<Expr *>> reads_;

public:
	explicit ProofDependencies(size_t p_locals) : reads_(p_locals) {}

	void replace(size_t p_local, const std::vector<Expr *> &p_reads) {
		auto &previous = reads_[p_local];
		for (Expr *expr : previous) {
			auto found = readers_.find(expr);
			if (found != readers_.end()) {
				found->second.erase(p_local);
				if (found->second.empty()) {
					readers_.erase(found);
				}
			}
		}
		previous.clear();
		for (Expr *expr : p_reads) {
			if (readers_[expr].insert(p_local).second) {
				previous.push_back(expr);
			}
		}
	}

	std::unordered_set<size_t> withdraw(Expr *p_expr) {
		auto found = readers_.extract(p_expr);
		return found.empty() ? std::unordered_set<size_t>() : std::move(found.mapped());
	}
};

} // namespace kas3
