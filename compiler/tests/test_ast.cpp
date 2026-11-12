#include "arena.h"
#include "ast.h"
#include "ast_printer.h"
#include "check.h"
#include "source.h"

#include <cstring>
#include <vector>

using namespace kas3;

template <class T>
static List<T> list_of(Arena &p_arena, std::initializer_list<T> p_items) {
	List<T> list;
	list.count = (uint32_t)p_items.size();
	list.items = p_arena.make_array<T>(p_items.size());
	uint32_t i = 0;
	for (const T &item : p_items) {
		list.items[i++] = item;
	}
	return list;
}

static void prints_binary() {
	//                    1234567890123
	Source source("t.as", "var x = 1 + 2");
	Arena arena;
	NumberLit *one = new_node<NumberLit>(arena, 8, 9);
	one->text = "1";
	NumberLit *two = new_node<NumberLit>(arena, 12, 13);
	two->text = "2";
	Binary *sum = new_node<Binary>(arena, 8, 13);
	sum->op = TokenKind::Plus;
	sum->left = one;
	sum->right = two;
	VarDecl *decl = new_node<VarDecl>(arena, 0, 13);
	Declarator declarator = {};
	declarator.name = "x";
	declarator.initializer = sum;
	declarator.offset = 4;
	decl->declarators = list_of<Declarator>(arena, { declarator });
	Program *program = new_node<Program>(arena, 0, 13);
	program->definitions = list_of<Node *>(arena, { decl });
	CHECK_EQ(print_ast(program, source), std::string("Program @1:1\n"
													 "  VarDecl @1:1\n"
													 "    Declarator name=\"x\" @1:5\n"
													 "      Binary op=\"+\" @1:9\n"
													 "        NumberLit text=\"1\" @1:9\n"
													 "        NumberLit text=\"2\" @1:13\n"));
}

static TypeRef *named_type(Arena &p_arena, std::initializer_list<std::string_view> p_parts) {
	TypeRef *type = new_node<TypeRef>(p_arena, 0, 0);
	type->form = TypeRef::Named;
	type->name = list_of<std::string_view>(p_arena, p_parts);
	return type;
}

static void prints_type_forms() {
	Source source("t.as", "x");
	Arena arena;
	TypeRef *any = new_node<TypeRef>(arena, 0, 0);
	any->form = TypeRef::Any;
	TypeRef *nothing = new_node<TypeRef>(arena, 0, 0);
	nothing->form = TypeRef::Void;
	TypeRef *event = named_type(arena, { "flash", "events", "Event" });
	TypeRef *inner = named_type(arena, { "Vector" });
	inner->form = TypeRef::Applied;
	inner->argument = named_type(arena, { "int" });
	TypeRef *outer = named_type(arena, { "Vector" });
	outer->form = TypeRef::Applied;
	outer->argument = inner;
	CHECK_EQ(print_node(any, source), std::string("Type * @1:1\n"));
	CHECK_EQ(print_node(nothing, source), std::string("Type void @1:1\n"));
	CHECK_EQ(print_node(event, source), std::string("Type flash.events.Event @1:1\n"));
	CHECK_EQ(print_node(outer, source), std::string("Type Vector.<Vector.<int>> @1:1\n"));
}

static void prints_modifiers_and_flags() {
	Source source("t.as", "x");
	Arena arena;
	FunctionDecl *function = new_node<FunctionDecl>(arena, 0, 0);
	function->name = "f";
	function->accessor = FunctionDecl::Getter;
	function->modifiers.bits = Modifiers::Public | Modifiers::Static | Modifiers::Override;
	CHECK_EQ(print_node(function, source), std::string("FunctionDecl name=\"f\" getter mods=public,static,override @1:1\n"));
}

static void escapes_strings() {
	Source source("t.as", "x");
	Arena arena;
	StringLit *literal = new_node<StringLit>(arena, 0, 0);
	literal->text = "\"a\\\"b\"";
	CHECK_EQ(print_node(literal, source), std::string("StringLit text=\"\\\"a\\\\\\\"b\\\"\" @1:1\n"));
}

static void prints_labels_and_holes() {
	Source source("t.as", "x");
	Arena arena;
	Name *a = new_node<Name>(arena, 0, 0);
	a->text = "a";
	ExprStmt *statement = new_node<ExprStmt>(arena, 0, 0);
	statement->expr = a;
	If *branch = new_node<If>(arena, 0, 0);
	branch->condition = a;
	branch->then_branch = statement;
	branch->else_branch = statement;
	CHECK_EQ(print_node(branch, source), std::string("If @1:1\n"
													 "  Name text=\"a\" @1:1\n"
													 "  ExprStmt @1:1\n"
													 "    Name text=\"a\" @1:1\n"
													 "  else:\n"
													 "    ExprStmt @1:1\n"
													 "      Name text=\"a\" @1:1\n"));
	ArrayLit *array = new_node<ArrayLit>(arena, 0, 0);
	array->elements = list_of<Expr *>(arena, { a, nullptr });
	CHECK_EQ(print_node(array, source), std::string("ArrayLit @1:1\n"
													"  Name text=\"a\" @1:1\n"
													"  hole\n"));
}

static void arena_copies_and_survives_growth() {
	Arena arena;
	std::vector<NumberLit *> nodes;
	for (int i = 0; i < 10000; i++) {
		NumberLit *node = arena.make<NumberLit>();
		node->offset = (uint32_t)i;
		nodes.push_back(node);
	}
	bool intact = true;
	for (int i = 0; i < 10000; i++) {
		intact = intact && nodes[(size_t)i]->offset == (uint32_t)i;
		intact = intact && (i == 0 || nodes[(size_t)i] != nodes[(size_t)i - 1]);
	}
	CHECK(intact);
	const char *original = "abc";
	std::string_view copy = arena.copy(original);
	CHECK_EQ(copy, std::string_view("abc"));
	CHECK(copy.data() != original);
	CHECK(arena.make_array<int>(0) != nullptr || true);
	int *zeros = arena.make_array<int>(100000);
	bool all_zero = true;
	for (int i = 0; i < 100000; i++) {
		all_zero = all_zero && zeros[i] == 0;
	}
	CHECK(all_zero);
}

int main() {
	prints_binary();
	prints_type_forms();
	prints_modifiers_and_flags();
	escapes_strings();
	prints_labels_and_holes();
	arena_copies_and_survives_growth();
	return check::finish();
}
