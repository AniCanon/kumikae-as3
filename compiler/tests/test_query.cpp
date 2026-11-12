// What the language server asks of the front end: checking through syntax errors, reusing a parse, and
// the queries on a checked program.
#include "check.h"

#include "ast_walk.h"
#include "builtins.h"
#include "names.h"

#include <filesystem>
#include <memory>

#include <string>
#include <vector>

using namespace kas3;

struct File {
	const char *path;
	std::string text;
	bool declaration = false;
};

static const Reference *reference(const Unit *p_unit, std::string_view p_name) {
	for (const Reference &item : p_unit->references) {
		if (item.name == p_name) {
			return &item;
		}
	}
	return nullptr;
}

static const char *const BROKEN =
		"package { import flash.utils.Dictionary; public class Main {\n"
		"var count:int;\n"
		"function f(other:Other):void { count = other.size; var d:Dictionary; if (count > }\n"
		"function g():void { count = \"text\"; }\n"
		"} }";
static const char *const OTHER = "package { public class Other { public var size:String; function h():void { size = 1; } } }";

static void test_tolerant() {
	// The plain check stops at a syntax error: nothing is resolved, and only that error is reported.
	Compilation plain;
	Unit *main = plain.add("Main.as", BROKEN);
	Unit *other = plain.add("Other.as", OTHER);
	plain.check();
	CHECK(main->references.empty());
	CHECK(other->references.empty());
	std::string expected = plain.format_diagnostics();
	CHECK_EQ(expected, std::string("Main.as:3:82: error: expected an expression but found '}'\n"));

	// The tolerant check resolves names in both files and reports the same.
	Compilation tolerant;
	main = tolerant.add("Main.as", BROKEN);
	other = tolerant.add("Other.as", OTHER);
	tolerant.check(true);
	CHECK_EQ(tolerant.format_diagnostics(), expected);
	const Reference *count = reference(main, "count");
	CHECK(count && count->symbol && count->symbol->kind == SymbolKind::Field);
	const Reference *type = reference(main, "Other");
	CHECK(type && type->symbol && type->symbol->unit == other);
	CHECK(reference(other, "size") != nullptr);
	CHECK(tolerant.has_errors());

	// With no syntax error the two are the same check.
	Compilation whole;
	whole.add("Other.as", OTHER);
	whole.check(true);
	CHECK_EQ(whole.format_diagnostics(),
			std::string("Other.as:1:83: error: can't use a value of type 'int' where 'String' is expected\n"));
}

static void test_import_symbol() {
	Compilation compilation;
	Unit *main = compilation.add("Main.as", BROKEN);
	compilation.check(true);
	Import *import = main->tree->package->imports[0];
	CHECK(import->symbol != nullptr);
	CHECK(import->symbol && import->symbol->name == "Dictionary");
	Compilation wildcard;
	main = wildcard.add("Main.as", "package { import flash.utils.*; import nowhere.Thing; public class Main {} }");
	wildcard.check();
	CHECK(main->tree->package->imports[0]->symbol == nullptr);
	CHECK(main->tree->package->imports[1]->symbol == nullptr);
}

// How many annotations the passes left on a tree.
static int annotations(Program *p_tree) {
	int count = 0;
	walk(p_tree, [&](Node *p_node) {
		if (is_expression(p_node->kind)) {
			count += static_cast<Expr *>(p_node)->type != nullptr;
		}
		switch (p_node->kind) {
			case NodeKind::TypeRef:
				count += static_cast<TypeRef *>(p_node)->symbol != nullptr;
				break;
			case NodeKind::Name:
				count += static_cast<Name *>(p_node)->symbol != nullptr;
				break;
			case NodeKind::Member:
				count += static_cast<Member *>(p_node)->symbol != nullptr;
				count += static_cast<Member *>(p_node)->member != nullptr;
				break;
			case NodeKind::VarDecl:
				for (const Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
					count += declarator.symbol != nullptr;
				}
				break;
			case NodeKind::FunctionDecl:
				count += static_cast<FunctionDecl *>(p_node)->symbol != nullptr;
				for (const Parameter &parameter : static_cast<FunctionDecl *>(p_node)->parameters) {
					count += parameter.symbol != nullptr;
				}
				break;
			case NodeKind::Try:
				for (const CatchClause &clause : static_cast<Try *>(p_node)->catches) {
					count += clause.symbol != nullptr;
				}
				break;
			case NodeKind::ClassDecl:
				count += static_cast<ClassDecl *>(p_node)->symbol != nullptr;
				break;
			case NodeKind::InterfaceDecl:
				count += static_cast<InterfaceDecl *>(p_node)->symbol != nullptr;
				break;
			case NodeKind::Import:
				count += static_cast<Import *>(p_node)->symbol != nullptr;
				break;
			default:
				break;
		}
	});
	return count;
}

struct Outcome {
	std::string diagnostics, names, types;
	bool operator==(const Outcome &p_other) const {
		return diagnostics == p_other.diagnostics && names == p_other.names && types == p_other.types;
	}
};
static std::ostream &operator<<(std::ostream &p_out, const Outcome &p_outcome) {
	return p_out << p_outcome.diagnostics << p_outcome.names.size() << " " << p_outcome.types.size();
}
static Outcome outcome(Compilation &p_compilation) {
	p_compilation.check(true);
	return { p_compilation.format_diagnostics(), p_compilation.dump_names(), p_compilation.dump_types() };
}

// A parsed file checked a second time gives what a fresh parse gives.
static void test_reuse() {
	std::vector<std::shared_ptr<Parsed>> builtins = parse_builtins();
	std::shared_ptr<Parsed> godot = parse_file(GODOT_DECLARATIONS_PATH, godot_declarations());
	std::vector<std::shared_ptr<Parsed>> corpus;
	for (const auto &entry : std::filesystem::directory_iterator(std::string(KAS3_TESTS_DIR) + "/corpus")) {
		std::string text;
		if (entry.path().extension() == ".as" && read_file(entry.path().string(), &text)) {
			corpus.push_back(parse_file(entry.path().filename().string(), text));
		}
	}
	CHECK_EQ(corpus.size(), (size_t)14);
	Outcome fresh;
	{
		Compilation compilation;
		compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
		for (const std::shared_ptr<Parsed> &file : corpus) {
			compilation.add(file->source.path(), file->source.text());
		}
		fresh = outcome(compilation);
		CHECK(fresh.names.size() > 1000);
		CHECK(fresh.types.size() > 1000);
	}
	for (int round = 0; round < 3; round++) {
		Compilation compilation(builtins);
		compilation.add(godot, true);
		for (const std::shared_ptr<Parsed> &file : corpus) {
			compilation.add(file);
		}
		CHECK_EQ(outcome(compilation), fresh);
	}
	// One file changes: the others keep their parse, and the result is that of a fresh check.
	std::string changed = corpus[0]->source.text() + "\nclass Extra { function f():void { var n:int = \"s\"; } }\n";
	Outcome fresh_changed;
	{
		Compilation compilation;
		compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
		compilation.add(corpus[0]->source.path(), changed);
		for (size_t i = 1; i < corpus.size(); i++) {
			compilation.add(corpus[i]->source.path(), corpus[i]->source.text());
		}
		fresh_changed = outcome(compilation);
		CHECK(fresh_changed.diagnostics.find("can't use a value of type 'String'") != std::string::npos);
	}
	{
		Compilation compilation(builtins);
		compilation.add(godot, true);
		compilation.add(parse_file(corpus[0]->source.path(), changed));
		for (size_t i = 1; i < corpus.size(); i++) {
			compilation.add(corpus[i]);
		}
		CHECK_EQ(outcome(compilation), fresh_changed);
	}
	// A name that resolved last time and does not this time is left unresolved, not pointing at the old
	// compilation's symbol.
	std::shared_ptr<Parsed> user = parse_file("A.as", "package { public class A { var b:B; function f():void { b.go(); } } }");
	auto resolved = [&]() {
		int count = 0;
		walk(user->tree, [&](Node *p_node) {
			count += p_node->kind == NodeKind::TypeRef && static_cast<TypeRef *>(p_node)->symbol != nullptr;
			count += p_node->kind == NodeKind::Member && static_cast<Member *>(p_node)->member != nullptr;
		});
		return count;
	};
	{
		Compilation compilation(builtins);
		compilation.add(user);
		compilation.add("B.as", "package { public class B { public function go():void {} } }");
		compilation.check(true);
		CHECK_EQ(resolved(), 2); // The type `B` and the member `go`.
	}
	{
		Compilation compilation(builtins);
		compilation.add(user);
		compilation.check(true);
		CHECK_EQ(resolved(), 0);
		CHECK(compilation.format_diagnostics().find("'B'") != std::string::npos);
	}
	// The wipe leaves nothing behind.
	CHECK(annotations(corpus[1]->tree) > 10);
	CHECK(annotations(godot->tree) > 10000);
	clear_annotations(corpus[1]->tree);
	clear_annotations(godot->tree);
	CHECK_EQ(annotations(corpus[1]->tree), 0);
	CHECK_EQ(annotations(godot->tree), 0);
}

int main() {
	test_tolerant();
	test_import_symbol();
	test_reuse();
	return check::finish();
}
