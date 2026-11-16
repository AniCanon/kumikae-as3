// What could be written at a place in a file: the language server's completion.
#include "query.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_set>

namespace kas3 {

namespace {

constexpr std::string_view IN_FUNCTION[] = {
	"var", "const", "if", "else", "for", "for each", "each", "in", "while", "do", "return", "break", "continue", "switch", "case",
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

} // namespace

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
	bool incomplete_ = false;
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
				if (p_type->symbol && p_type->symbol->kind == SymbolKind::Interface) {
					// The object is of some class, and every class has what every object has, as the checker says.
					offer_members(compilation_.object_class(), false, false);
				}
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
			case Type::Any:
			case Type::Unknown:
				// Whatever it is, it is an object: what every object has is all that is known.
				offer_members(compilation_.object_class(), false, false);
				break;
			default:
				break;
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
		std::sort(sorted.begin(), sorted.end(), [&](const Symbol *p_a, const Symbol *p_b) {
			return p_a->name != p_b->name ? p_a->name < p_b->name :
					compilation_.qualified_name(p_a) < compilation_.qualified_name(p_b);
		});
		for (Symbol *symbol : sorted) {
			const Found found = compilation_.lookup_package_level(context, symbol->name);
			if (found.first == symbol && !found.second) {
				offer(symbol);
			}
		}
		for (Symbol *symbol : sorted) {
			const Found found = compilation_.lookup_package_level(context, symbol->name);
			if (symbol->is_type() && !symbol->file_private && symbol->package &&
					!symbol->package->name.empty() && !found.first && !seen_.count(symbol->name) &&
					compilation_.visible(symbol, context)) {
				if (prefix_.empty()) {
					incomplete_ = true;
					continue;
				}
				Completion item = queries_.completion_item(symbol);
				const std::string qualified = compilation_.qualified_name(symbol);
				item.detail += " (import " + qualified + ")";
				item.additional_edit = queries_.import_edit(unit_, offset_, qualified);
				found_.push_back(std::move(item));
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
		offer_package(std::string(source_.substr(begin, start_ - begin)), false);
		return true;
	}

	// What a package's path names, dot included ("flash.utils."): what that package holds that can be
	// reached from here, and the packages whose names go on from the path.
	void offer_package(std::string_view p_path, bool p_types_only) {
		const Context context = compilation_.context_of(const_cast<Unit &>(unit_), true);
		std::unordered_set<const Package *> seen;
		for (Unit &unit : compilation_.units()) {
			if (!unit.package || !seen.insert(unit.package).second) {
				continue;
			}
			const std::string qualified = unit.package->name + ".";
			if (qualified == p_path) {
				std::vector<Symbol *> sorted;
				for (const auto &[name, symbol] : unit.package->definitions) {
					if (compilation_.visible(symbol, context) && (!p_types_only || symbol->is_type())) {
						sorted.push_back(symbol);
					}
				}
				std::sort(sorted.begin(), sorted.end(), [](const Symbol *p_a, const Symbol *p_b) { return p_a->name < p_b->name; });
				for (Symbol *symbol : sorted) {
					offer(symbol);
				}
			} else if (qualified.compare(0, p_path.size(), p_path) == 0) {
				const std::string_view name = std::string_view(unit.package->name).substr(p_path.size(), qualified.find('.', p_path.size()) - p_path.size());
				if (!name.empty() && starts_with_any_case(name, prefix_) && seen_.insert(name).second) {
					Completion item;
					item.name = std::string(name);
					item.is_package = true;
					found_.push_back(std::move(item));
				}
			}
		}
	}

	// After a dot: what the value before it has, when its type is known (`godot.Vector2.` is a class).
	// Otherwise, if its first name means nothing here, it may be a package's path; if it does, it is a
	// value of a type not known.
	void offer_after_dot(uint32_t p_dot) {
		const Member *member = member_after(p_dot);
		const Expr *root = member ? member->object : nullptr;
		while (root && root->kind == NodeKind::Member) {
			root = static_cast<const Member *>(root)->object;
		}
		const Type *type = member ? member->object->type : nullptr;
		if (type && type->kind != Type::Unknown) {
			offer_of_type(type);
			return;
		}
		if (root && !(root->kind == NodeKind::Name && !static_cast<const Name *>(root)->symbol)) {
			offer_of_type(type);
			return;
		}
		uint32_t begin = p_dot;
		while (begin > 0 && (is_identifier_at(source_, begin - 1) || source_[begin - 1] == '.')) {
			--begin;
		}
		const std::string path(source_.substr(begin, p_dot + 1 - begin));
		if (is_identifier_at(path, 0) && path.find("..") == std::string::npos) {
			offer_package(path, type_ends_at(p_dot) || at_type());
		}
	}

	// True if a type written in the tree ends at the offset: `var d:flash.utils.` is a type's path.
	bool type_ends_at(uint32_t p_offset) const {
		for (const Node *node : queries_.context_nodes(unit_)) {
			if (node->kind == NodeKind::TypeRef && node->end == p_offset) {
				return true;
			}
		}
		return false;
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

	// The `a.b` whose dot is at p_dot: of the names written after that dot, the first, whose value ends
	// right before the dot. None if the dot is not in a member's place (a type's path).
	Member *member_after(uint32_t p_dot) {
		Member *found = nullptr;
		std::for_each(queries_.context_nodes(unit_).begin(), queries_.context_nodes(unit_).end(), [&](Node *p_node) {
			if (p_node->kind == NodeKind::Member) {
				Member *member = static_cast<Member *>(p_node);
				uint32_t gap = member->object ? member->object->end : p_dot + 1;
				while (gap < p_dot && (std::isspace((unsigned char)source_[gap]) || queries_.text_byte(unit_, gap))) {
					gap++;
				}
				if (gap == p_dot && member->name_offset > p_dot && (!found || member->name_offset < found->name_offset)) {
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

	// In a call's argument, the name in reach that the parameter there has goes first: most often it is
	// what is meant.
	void put_parameter_name_first() {
		const std::optional<SignatureHelp> help = queries_.signature_help(unit_, offset_);
		if (!help || help->active_name.empty()) {
			return;
		}
		auto match = std::find_if(found_.begin(), found_.end(), [&](const Completion &p_item) { return p_item.name == help->active_name; });
		if (match != found_.end()) {
			match->is_parameter_name = true;
			std::rotate(found_.begin(), match, match + 1);
		}
	}

	// Whether a type is what is written here: `var a:`, `):`, `new`, `extends`, `implements`, `is`, `as`.
	// The tree says so wherever the grammar wants a type, even before one is started.
	bool at_type() const {
		const uint32_t end = before();
		if (end == 0) {
			return false;
		}
		for (const Node *node : queries_.context_nodes(unit_)) {
			if (node->kind == NodeKind::TypeRef) {
				const auto *type = static_cast<const TypeRef *>(node);
				if (type->completion_begin <= offset_ && offset_ <= std::max(type->offset, type->end)) {
					return true;
				}
			}
		}
		const std::string_view keyword = word_before(end);
		return keyword == "new" || keyword == "extends" || keyword == "implements" || keyword == "is" || keyword == "as";
	}

	// Inside a function's body: locals, members, definitions, keywords and statement templates.
	void offer_in_body() {
		const bool is_static = function_->symbol && function_->symbol->is_static();
		offer_locals();
		if (class_) {
			if (!is_static) {
				offer_members(class_, false, true);
			}
			offer_members(class_, true, true);
		}
		offer_definitions(false);
		put_parameter_name_first();
		offer_keywords(IN_FUNCTION);
		queries_.statement_templates(unit_, offset_, found_);
		// Typing a keyword can turn a recovery gap into a known statement start.
		incomplete_ |= prefix_.empty();
		if (class_ && !is_static && starts_with_any_case("this", prefix_)) {
			Completion item;
			item.name = "this";
			item.is_keyword = true;
			found_.push_back(std::move(item));
		}
	}

public:
	Completer(Queries &p_queries, Compilation &p_compilation, const Unit &p_unit, uint32_t p_offset) :
			queries_(p_queries), compilation_(p_compilation), unit_(p_unit), source_(p_unit.source.text()), offset_(p_offset) {}

	bool incomplete() const { return incomplete_; }

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
		if (class_ && function_ && (function_->modifiers.bits & Modifiers::Override) &&
				(function_->name.empty() ? offset_ > function_->name_offset :
						offset_ >= function_->name_offset && offset_ <= function_->name_offset + function_->name.size())) {
			std::optional<OverrideOffer> offer = queries_.overrides(unit_, function_, class_, start_, offset_);
			return offer ? std::move(offer->items) : std::vector<Completion>();
		}
		if (offer_import()) {
			return std::move(found_);
		}
		const uint32_t end = before();
		if (end > 0 && source_[end - 1] == '.') {
			offer_after_dot(end - 1);
			return std::move(found_);
		}
		if (at_type()) {
			offer_definitions(true);
			return std::move(found_);
		}
		if (function_ && in_body_) {
			offer_in_body();
		} else if (class_) {
			if (std::optional<OverrideOffer> offer = queries_.overrides(unit_, nullptr, class_, start_, offset_)) {
				std::move(offer->items.begin(), offer->items.end(), std::back_inserter(found_));
				if (offer->after_function) {
					return std::move(found_);
				}
			}
			offer_keywords(IN_CLASS);
		} else {
			offer_keywords(IN_FILE);
		}
		return std::move(found_);
	}
};

CompletionResult Queries::complete(const Unit &p_unit, uint32_t p_offset) {
	Completer completer(*this, compilation_, p_unit, p_offset);
	auto items = completer.run();
	return { std::move(items), completer.incomplete() };
}

} // namespace kas3
