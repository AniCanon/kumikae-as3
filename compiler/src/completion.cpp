// What could be written at a place in a file: the language server's completion.
#include "query.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace kas3 {

namespace {

constexpr std::string_view IN_FUNCTION[] = {
	"var", "const", "if", "else", "for", "each", "in", "while", "do", "return", "break", "continue", "switch", "case",
	"default", "new", "null", "true", "false", "is", "as", "typeof", "function", "throw", "try", "catch", "finally",
	"super", "delete", "void",
};
constexpr std::string_view IN_CLASS[] = {
	"public", "private", "protected", "internal", "static", "override", "final", "function", "var", "const", "native",
	"import", "get", "set",
};
constexpr std::string_view IN_FILE[] = {
	"package", "import", "public", "internal", "class", "interface", "extends", "implements", "function", "var",
	"const", "final", "dynamic",
};

bool starts_with_any_case(std::string_view p_name, std::string_view p_prefix) {
	if (p_name.size() < p_prefix.size()) {
		return false;
	}
	for (size_t i = 0; i < p_prefix.size(); i++) {
		if (std::tolower((unsigned char)p_name[i]) != std::tolower((unsigned char)p_prefix[i])) {
			return false;
		}
	}
	return true;
}

} // namespace

// One completion request: where the cursor is, and what is collected for it.
class Completer {
	Queries &queries_;
	Compilation &compilation_;
	const Unit &unit_;
	std::string_view source_;
	uint32_t offset_;
	uint32_t start_ = 0; // Where the word being typed begins.
	std::string_view prefix_;
	Symbol *class_ = nullptr; // The class the cursor is in.
	FunctionDecl *function_ = nullptr; // The outermost function the cursor is in.
	bool in_body_ = false;
	std::vector<Completion> found_;
	std::unordered_set<std::string_view> seen_;

	void offer(Symbol *p_symbol) {
		if (!p_symbol || p_symbol->name.empty() || !starts_with_any_case(p_symbol->name, prefix_) ||
				!seen_.insert(p_symbol->name).second) {
			return;
		}
		found_.push_back(queries_.completion_item(p_symbol));
	}

	template <size_t N>
	void offer_keywords(const std::string_view (&p_words)[N]) {
		for (std::string_view word : p_words) {
			if (starts_with_any_case(word, prefix_) && seen_.insert(word).second) {
				Completion item;
				item.name = std::string(word);
				item.is_keyword = true;
				found_.push_back(std::move(item));
			}
		}
	}

	// Whether code in the cursor's class may name a member.
	bool reachable(const Symbol *p_member) const {
		if (p_member->modifiers & Modifiers::Private) {
			return p_member->owner == class_;
		}
		if (p_member->modifiers & Modifiers::Protected) {
			for (const Symbol *at = class_; at && at->klass; at = at->klass->base) {
				if (at == p_member->owner) {
					return true;
				}
			}
			return false;
		}
		return true;
	}

	// A class's members and its bases', nearest first, so that an override is offered once, as the nearest.
	void offer_members(Symbol *p_class, bool p_static, bool p_inherited) {
		std::vector<Symbol *> sorted;
		std::unordered_set<const Symbol *> visited;
		std::vector<Symbol *> pending = { p_class };
		while (!pending.empty()) {
			Symbol *at = pending.back();
			pending.pop_back();
			if (!at || !at->klass || !visited.insert(at).second) {
				continue;
			}
			sorted.clear();
			for (const auto &[name, member] : at->klass->table(p_static)) {
				if (reachable(member)) {
					sorted.push_back(member);
				}
			}
			std::sort(sorted.begin(), sorted.end(), [](const Symbol *p_a, const Symbol *p_b) { return p_a->name < p_b->name; });
			for (Symbol *member : sorted) {
				offer(member);
			}
			if (p_inherited) {
				for (Symbol *interface : at->klass->interfaces) {
					pending.push_back(interface);
				}
				pending.push_back(at->klass->base);
			}
		}
	}

	void offer_of_type(const Type *p_type) {
		if (!p_type) {
			return;
		}
		switch (p_type->kind) {
			case Type::Instance:
				offer_members(p_type->symbol, false, true);
				break;
			case Type::Vector:
				offer_members(compilation_.vector_class(), false, true);
				break;
			case Type::ClassObject:
				// What the class itself has. AS3 does not pass statics down to a subclass's name.
				if (p_type->element && p_type->element->kind == Type::Instance) {
					offer_members(p_type->element->symbol, true, false);
				}
				break;
			default:
				break; // Nothing is known of it: offering everything would be a guess.
		}
	}

	// The definitions a bare name could mean from here: each is asked for by its own name, so that what
	// is offered is exactly what would resolve.
	void offer_definitions(bool p_types_only) {
		Unit &unit = const_cast<Unit &>(unit_);
		Context context = class_ ? compilation_.context_of(class_) : compilation_.context_of(unit, true);
		if (const LocalScope *scope = queries_.local_scope_at(unit_, offset_)) {
			context.imports = const_cast<ImportScope *>(&scope->imports);
		}
		std::vector<Symbol *> sorted;
		std::unordered_set<const Package *> packages;
		auto take = [&](Symbol *p_symbol) {
			if ((!p_types_only || p_symbol->is_type()) && starts_with_any_case(p_symbol->name, prefix_)) {
				sorted.push_back(p_symbol);
			}
		};
		for (Unit &other : compilation_.units()) {
			if (other.package && packages.insert(other.package).second) {
				for (const auto &[name, symbol] : other.package->definitions) {
					take(symbol);
				}
			}
		}
		for (const auto &[name, symbol] : unit_.helpers) {
			take(symbol);
		}
		std::sort(sorted.begin(), sorted.end(), [](const Symbol *p_a, const Symbol *p_b) { return p_a->name < p_b->name; });
		for (Symbol *symbol : sorted) {
			if (compilation_.lookup_package_level(context, symbol->name).first == symbol) {
				offer(symbol);
			}
		}
	}

	bool offer_import() {
		uint32_t begin = start_;
		while (begin > 0 && (is_identifier_at(source_, begin - 1) || source_[begin - 1] == '.')) {
			--begin;
		}
		uint32_t keyword_end = begin;
		while (keyword_end > 0 && std::isspace(static_cast<unsigned char>(source_[keyword_end - 1]))) {
			--keyword_end;
		}
		if (word_before(keyword_end) != "import") {
			return false;
		}
		const std::string path(source_.substr(begin, start_ - begin));
		const Context context = compilation_.context_of(const_cast<Unit &>(unit_), true);
		std::unordered_set<const Package *> seen;
		for (Unit &unit : compilation_.units()) {
			if (!unit.package || !seen.insert(unit.package).second) {
				continue;
			}
			const std::string qualified = unit.package->name + ".";
			if (qualified == path) {
				std::vector<Symbol *> sorted;
				for (const auto &[name, symbol] : unit.package->definitions) {
					if (compilation_.visible(symbol, context)) {
						sorted.push_back(symbol);
					}
				}
				std::sort(sorted.begin(), sorted.end(), [](const Symbol *p_a, const Symbol *p_b) { return p_a->name < p_b->name; });
				for (Symbol *symbol : sorted) {
					offer(symbol);
				}
			} else if (qualified.compare(0, path.size(), path) == 0) {
				const std::string_view name = std::string_view(unit.package->name).substr(path.size(), qualified.find('.', path.size()) - path.size());
				if (!name.empty() && starts_with_any_case(name, prefix_) && seen_.insert(name).second) {
					Completion item;
					item.name = std::string(name);
					item.is_package = true;
					found_.push_back(std::move(item));
				}
			}
		}
		return true;
	}

	void offer_locals() {
		const LocalScope *scope = queries_.local_scope_at(unit_, offset_);
		for (; scope; scope = scope->parent) {
			std::vector<Symbol *> sorted;
			for (const auto &[name, symbol] : scope->names) {
				sorted.push_back(symbol);
			}
			std::sort(sorted.begin(), sorted.end(), [](const Symbol *p_a, const Symbol *p_b) { return p_a->name < p_b->name; });
			for (Symbol *symbol : sorted) {
				offer(symbol);
			}
		}
	}

	void find_enclosing() {
		std::for_each(queries_.context_nodes(unit_).begin(), queries_.context_nodes(unit_).end(), [&](Node *p_node) {
			if (p_node->offset > offset_ || p_node->end < offset_) {
				return;
			}
			if (p_node->kind == NodeKind::ClassDecl) {
				class_ = static_cast<ClassDecl *>(p_node)->symbol;
			} else if (p_node->kind == NodeKind::InterfaceDecl) {
				class_ = static_cast<InterfaceDecl *>(p_node)->symbol;
			} else if (p_node->kind == NodeKind::FunctionDecl && !function_) {
				FunctionDecl *function = static_cast<FunctionDecl *>(p_node);
				function_ = function;
				in_body_ = function->body && function->body->offset < offset_ && offset_ <= function->body->end;
			}
		});
	}

	// The `a.b` whose dot is at p_dot: of the names written after that dot, the first.
	Member *member_after(uint32_t p_dot) {
		Member *found = nullptr;
		std::for_each(queries_.context_nodes(unit_).begin(), queries_.context_nodes(unit_).end(), [&](Node *p_node) {
			if (p_node->kind == NodeKind::Member) {
				Member *member = static_cast<Member *>(p_node);
				if (member->name_offset > p_dot && (!found || member->name_offset < found->name_offset)) {
					found = member;
				}
			}
		});
		return found;
	}

	// The last thing written before the word being typed, skipping spaces: one character of it.
	uint32_t before() const {
		uint32_t at = start_;
		while (at > 0 && std::isspace((unsigned char)source_[at - 1])) {
			at--;
		}
		return at;
	}

	std::string_view word_before(uint32_t p_end) const {
		uint32_t begin = p_end;
		while (begin > 0 && is_identifier_at(source_, begin - 1)) {
			begin--;
		}
		return source_.substr(begin, p_end - begin);
	}

	// Whether a type is what is written here: `var a:`, `):`, `new`, `extends`, `implements`, `is`, `as`.
	bool at_type() const {
		const uint32_t end = before();
		if (end == 0) {
			return false;
		}
		if (source_[end - 1] == ':') {
			uint32_t name_end = end - 1;
			while (name_end > 0 && std::isspace((unsigned char)source_[name_end - 1])) {
				name_end--;
			}
			if (name_end > 0 && source_[name_end - 1] == ')') {
				return true; // A function's result.
			}
			// `name:` is a type's place after `var`, `const`, or in a parameter list. In `a ? b : c` and
			// `{ key: value }` it is not: there the name follows `?`, `{` or a value.
			const std::string_view name = word_before(name_end);
			uint32_t lead = name_end - (uint32_t)name.size();
			while (lead > 0 && std::isspace((unsigned char)source_[lead - 1])) {
				lead--;
			}
			const std::string_view keyword = word_before(lead);
			return !name.empty() && (keyword == "var" || keyword == "const" || (function_ && !in_body_));
		}
		const std::string_view keyword = word_before(end);
		return keyword == "new" || keyword == "extends" || keyword == "implements" || keyword == "is" || keyword == "as";
	}

public:
	Completer(Queries &p_queries, Compilation &p_compilation, const Unit &p_unit, uint32_t p_offset) :
			queries_(p_queries), compilation_(p_compilation), unit_(p_unit), source_(p_unit.source.text()), offset_(p_offset) {}

	std::vector<Completion> run() {
		if (offset_ > source_.size() || queries_.in_text(unit_, offset_)) {
			return {};
		}
		start_ = offset_;
		while (start_ > 0 && is_identifier_at(source_, start_ - 1)) {
			start_--;
		}
		prefix_ = source_.substr(start_, offset_ - start_);
		find_enclosing();
		if (offer_import()) {
			return std::move(found_);
		}
		const uint32_t end = before();
		if (end > 0 && source_[end - 1] == '.') {
			if (Member *member = member_after(end - 1); member && member->object && !member->symbol) {
				offer_of_type(member->object->type);
			}
			return std::move(found_);
		}
		if (at_type()) {
			offer_definitions(true);
			return std::move(found_);
		}
		if (function_ && in_body_) {
			const bool is_static = function_->symbol && function_->symbol->is_static();
			offer_locals();
			if (class_) {
				if (!is_static) {
					offer_members(class_, false, true);
				}
				offer_members(class_, true, true);
			}
			offer_definitions(false);
			offer_keywords(IN_FUNCTION);
			if (class_ && !is_static && starts_with_any_case("this", prefix_)) {
				Completion item;
				item.name = "this";
				item.is_keyword = true;
				found_.push_back(std::move(item));
			}
		} else if (class_) {
			offer_keywords(IN_CLASS);
		} else {
			offer_keywords(IN_FILE);
		}
		return std::move(found_);
	}
};

std::vector<Completion> Queries::complete(const Unit &p_unit, uint32_t p_offset) {
	return Completer(*this, compilation_, p_unit, p_offset).run();
}

} // namespace kas3
