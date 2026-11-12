#include "arena.h"
#include "ast_printer.h"
#include "check.h"
#include "diagnostics.h"
#include "parser.h"
#include "source.h"

#include <string>

using namespace kas3;

struct Parsed {
	std::string tree; // Printed tree with the " @line:column" suffixes removed.
	std::string errors;
};

static std::string strip_positions(const std::string &p_text) {
	std::string out;
	size_t line_start = 0;
	while (line_start < p_text.size()) {
		size_t line_end = p_text.find('\n', line_start);
		std::string line = p_text.substr(line_start, line_end - line_start);
		size_t at = line.rfind(" @");
		out += at == std::string::npos ? line : line.substr(0, at);
		out += '\n';
		line_start = line_end + 1;
	}
	return out;
}

static Parsed expr(const std::string &p_text) {
	Source source("t.as", p_text);
	Arena arena;
	Diagnostics diagnostics;
	Expr *tree = parse_expression_for_test(source, arena, diagnostics);
	return Parsed{ strip_positions(print_node(tree, source)), diagnostics.format(source) };
}

static Parsed stmt(const std::string &p_text) {
	Source source("t.as", p_text);
	Arena arena;
	Diagnostics diagnostics;
	Stmt *tree = parse_statement_for_test(source, arena, diagnostics);
	return Parsed{ strip_positions(print_node(tree, source)), diagnostics.format(source) };
}

static Parsed program(const std::string &p_text) {
	Source source("t.as", p_text);
	Arena arena;
	Diagnostics diagnostics;
	Program *tree = parse(source, arena, diagnostics);
	CHECK(tree != nullptr);
	return Parsed{ strip_positions(print_ast(tree, source)), diagnostics.format(source) };
}

static bool has(const std::string &p_text, const char *p_part) {
	return p_text.find(p_part) != std::string::npos;
}

static int count(const std::string &p_text, const char *p_part) {
	int n = 0;
	for (size_t at = p_text.find(p_part); at != std::string::npos; at = p_text.find(p_part, at + 1)) {
		n++;
	}
	return n;
}

#define NO_ERRORS(parsed) CHECK_EQ((parsed).errors, std::string(""))

// ---- Task 4: expressions and types ----

static void precedence() {
	CHECK_EQ(expr("1 + 2 * 3").tree, std::string(R"(Binary op="+"
  NumberLit text="1"
  Binary op="*"
    NumberLit text="2"
    NumberLit text="3"
)"));
	CHECK_EQ(expr("a = b = c").tree, std::string(R"(Assign op="="
  Name text="a"
  Assign op="="
    Name text="b"
    Name text="c"
)"));
	CHECK_EQ(expr("a ? b : c ? d : e").tree, std::string(R"(Conditional
  Name text="a"
  Name text="b"
  Conditional
    Name text="c"
    Name text="d"
    Name text="e"
)"));
	CHECK_EQ(expr("a || b && c").tree, std::string(R"(Binary op="||"
  Name text="a"
  Binary op="&&"
    Name text="b"
    Name text="c"
)"));
	CHECK_EQ(expr("-a.b").tree, std::string(R"(Unary op="-"
  Member name="b"
    Name text="a"
)"));
	CHECK_EQ(expr("a is B as C").tree, std::string(R"(Binary op="as"
  Binary op="is"
    Name text="a"
    Name text="B"
  Name text="C"
)"));
	CHECK_EQ(expr("1 - 2 - 3").tree, std::string(R"(Binary op="-"
  Binary op="-"
    NumberLit text="1"
    NumberLit text="2"
  NumberLit text="3"
)"));
	CHECK_EQ(expr("a, b").tree, std::string("Binary op=\",\"\n  Name text=\"a\"\n  Name text=\"b\"\n"));
	CHECK_EQ(expr("a += b >>>= c").tree, std::string(R"(Assign op="+="
  Name text="a"
  Assign op=">>>="
    Name text="b"
    Name text="c"
)"));
	CHECK_EQ(expr("typeof a == \"x\"").tree, std::string(R"(Binary op="=="
  Unary op="typeof"
    Name text="a"
  StringLit text="\"x\""
)"));
}

static void calls_and_members() {
	Parsed result = expr("a.b(c)[d].e");
	NO_ERRORS(result);
	CHECK_EQ(result.tree, std::string(R"(Member name="e"
  Index
    Call
      Member name="b"
        Name text="a"
      Name text="c"
    Name text="d"
)"));
	NO_ERRORS(expr("super.update(dt)"));
	NO_ERRORS(expr("f()()(1, 2)"));
}

static void reserved_word_after_dot() {
	for (const char *text : { "event.type", "obj.default", "x.class", "a.new.in" }) {
		Parsed result = expr(text);
		NO_ERRORS(result);
		CHECK(has(result.tree, "Member name="));
	}
	CHECK(has(expr("obj.default").tree, "Member name=\"default\""));
}

static void new_forms() {
	CHECK_EQ(expr("new Foo").tree, std::string("New\n  Name text=\"Foo\"\n"));
	CHECK_EQ(expr("new a.B(1, 2)").tree, std::string(R"(New has_arguments
  Member name="B"
    Name text="a"
  NumberLit text="1"
  NumberLit text="2"
)"));
	CHECK_EQ(expr("new Vector.<int>()").tree, std::string(R"(New has_arguments
  TypeApply
    Name text="Vector"
    Type int
)"));
	CHECK_EQ(expr("new <int>[1, 2]").tree, std::string(R"(VectorLit
  Type int
  NumberLit text="1"
  NumberLit text="2"
)"));
	CHECK_EQ(expr("new Foo().bar").tree, std::string(R"(Member name="bar"
  New has_arguments
    Name text="Foo"
)"));
}

static void nested_type_arguments() {
	Parsed nested = expr("new Vector.<Vector.<int>>()");
	NO_ERRORS(nested);
	CHECK(has(nested.tree, "Type Vector.<int>"));
	NO_ERRORS(expr("new Vector.<Vector.<Vector.<int>>>()"));
	std::string deep_type = "new ";
	for (int i = 0; i < 200; i++) {
		deep_type += "Vector.<";
	}
	Parsed too_deep_type = expr(deep_type + "int" + std::string(200, '>') + "()");
	CHECK_EQ(count(too_deep_type.errors, "error:"), 1);
	CHECK_EQ(count(too_deep_type.errors, "code is nested too deeply"), 1);
	NO_ERRORS(expr("x = new Vector.<Vector.<int>>= y")); // `>>=` closes two lists and leaves `=`
	CHECK_EQ(expr("a >> b").tree, std::string("Binary op=\">>\"\n  Name text=\"a\"\n  Name text=\"b\"\n"));
	CHECK_EQ(expr("a >>> b").tree, std::string("Binary op=\">>>\"\n  Name text=\"a\"\n  Name text=\"b\"\n"));
	CHECK_EQ(expr("a >= b").tree, std::string("Binary op=\">=\"\n  Name text=\"a\"\n  Name text=\"b\"\n"));
	CHECK_EQ(expr("a >>= b").tree, std::string("Assign op=\">>=\"\n  Name text=\"a\"\n  Name text=\"b\"\n"));
	CHECK(has(expr("Vector.<int>(list)").tree, "TypeApply"));
}

static void literals() {
	CHECK_EQ(expr("[1, , \"x\"]").tree, std::string(R"(ArrayLit
  NumberLit text="1"
  hole
  StringLit text="\"x\""
)"));
	CHECK_EQ(expr("[1, 2,]").tree, std::string("ArrayLit\n  NumberLit text=\"1\"\n  NumberLit text=\"2\"\n"));
	Parsed object = expr("{ a: 1, \"b\": 2, default: 3, 4: 5 }");
	NO_ERRORS(object);
	CHECK_EQ(object.tree, std::string(R"(ObjectLit
  Field key="a"
    NumberLit text="1"
  Field key="b" string
    NumberLit text="2"
  Field key="default"
    NumberLit text="3"
  Field key="4"
    NumberLit text="5"
)"));
	CHECK_EQ(expr("{}").tree, std::string("ObjectLit\n"));
	CHECK_EQ(expr("true").tree, std::string("BoolLit true\n"));
	CHECK_EQ(expr("false").tree, std::string("BoolLit false\n"));
	CHECK_EQ(expr("null").tree, std::string("NullLit\n"));
	CHECK_EQ(expr("this").tree, std::string("ThisExpr\n"));
}

static void regex_where_an_expression_starts() {
	CHECK_EQ(expr("/ab+c/gi").tree, std::string("RegexLit text=\"/ab+c/gi\"\n"));
	CHECK_EQ(expr("a / b / c").tree, std::string(R"(Binary op="/"
  Binary op="/"
    Name text="a"
    Name text="b"
  Name text="c"
)"));
	CHECK_EQ(expr("x = /=/").tree, std::string("Assign op=\"=\"\n  Name text=\"x\"\n  RegexLit text=\"/=/\"\n"));
	NO_ERRORS(expr("x = /\"/"));
	NO_ERRORS(expr("x = /'/"));
	NO_ERRORS(expr("s.replace(/\\s+/g, \" \")"));
}

static void postfix_needs_same_line() {
	CHECK_EQ(expr("a++").tree, std::string("Postfix op=\"++\"\n  Name text=\"a\"\n"));
	Parsed split = expr("a\n++b");
	CHECK_EQ(split.tree, std::string("Name text=\"a\"\n"));
	CHECK_EQ(split.errors, std::string("t.as:2:1: error: expected end of file but found '++'\n"));
}

static void expression_errors() {
	CHECK_EQ(expr("1 +").errors, std::string("t.as:1:4: error: expected an expression but found end of file\n"));
	CHECK_EQ(expr("(1 + 2").errors, std::string("t.as:1:1: error: '(' is never closed\n"));
	CHECK_EQ(expr("f(1, 2").errors, std::string("t.as:1:2: error: '(' is never closed\n"));
	CHECK_EQ(expr("a[1").errors, std::string("t.as:1:2: error: '[' is never closed\n"));
	CHECK_EQ(expr("[1, 2").errors, std::string("t.as:1:1: error: '[' is never closed\n"));
	CHECK_EQ(expr("{ a: 1").errors, std::string("t.as:1:1: error: '{' is never closed\n"));
	CHECK_EQ(expr("(1 + 2 ]").errors, std::string("t.as:1:8: error: expected ')' but found ']'\n"));
	CHECK_EQ(expr("a.").errors, std::string("t.as:1:3: error: expected a property name but found end of file\n"));
	CHECK_EQ(expr("x = 12abc").errors, std::string("t.as:1:5: error: invalid number\n"));
}

static void depth_and_length_caps() {
	std::string nested(300, '(');
	Parsed deep = expr(nested);
	CHECK_EQ(count(deep.errors, "code is nested too deeply"), 1);
	// Past the cap the parser stops: one diagnostic, not one per unclosed bracket.
	CHECK_EQ(count(deep.errors, "error:"), 1);
	Parsed balanced = stmt("{ var x:int = " + nested + "1" + std::string(300, ')') + "; var y:int = 2; }");
	CHECK_EQ(count(balanced.errors, "error:"), 1);
	CHECK_EQ(count(balanced.errors, "code is nested too deeply"), 1);
	std::string unary(1000, '-');
	CHECK_EQ(count(expr(unary + "a").errors, "code is nested too deeply"), 1);

	std::string sum = "1";
	for (int i = 0; i < 100000; i++) {
		sum += "+1";
	}
	Parsed long_sum = expr(sum);
	CHECK_EQ(count(long_sum.errors, "expression is too long"), 1);
	CHECK_EQ(count(long_sum.errors, "error:"), 1);
	std::string members = "a";
	for (int i = 0; i < 100000; i++) {
		members += ".b";
	}
	CHECK_EQ(count(expr(members).errors, "expression is too long"), 1);
	std::string calls = "a";
	for (int i = 0; i < 100000; i++) {
		calls += "()";
	}
	CHECK_EQ(count(expr(calls).errors, "expression is too long"), 1);
	std::string fine = "1";
	for (int i = 0; i < 400; i++) {
		fine += "+1";
	}
	NO_ERRORS(expr(fine));
}

static void deep_and_long_inputs_do_not_crash() {
	// `new` and `@` used to recurse without counting.
	std::string news;
	for (int i = 0; i < 50000; i++) {
		news += "new ";
	}
	Parsed chained_new = expr(news + "A");
	CHECK_EQ(count(chained_new.errors, "code is nested too deeply"), 1);
	CHECK_EQ(count(chained_new.errors, "error:"), 1);
	Parsed chained_at = expr(std::string(300000, '@') + "a");
	CHECK_EQ(count(chained_at.errors, "error:"), 1);
	CHECK_EQ(count(chained_at.errors, "E4X"), 1);

	// A long chain inside nested function bodies: the height of the whole tree is what must stay bounded.
	std::string sum = "1";
	for (int i = 0; i < 400; i++) {
		sum += "+1";
	}
	std::string nested = sum;
	for (int i = 0; i < 60; i++) {
		nested = "function():* { return " + nested + "+" + sum + "; }";
	}
	Source source("t.as", "var f:Function = " + nested + ";");
	Arena arena;
	Diagnostics diagnostics;
	Program *tree = parse(source, arena, diagnostics);
	std::string printed = print_ast(tree, source);
	size_t deepest = 0;
	size_t line_start = 0;
	while (line_start < printed.size()) {
		size_t indent = printed.find_first_not_of(' ', line_start) - line_start;
		deepest = indent > deepest ? indent : deepest;
		line_start = printed.find('\n', line_start) + 1;
	}
	CHECK_EQ(deepest / 2 <= 600 ? (size_t)0 : deepest / 2, (size_t)0);
	CHECK_EQ(count(diagnostics.format(source), "expression is too long"), 1);
	CHECK(count(diagnostics.format(source), "error:") <= 2);

	// Named nested functions and parameter defaults count toward the same bound.
	auto depth_of = [](const std::string &p_text, std::string *r_errors) {
		Source text_source("t.as", p_text);
		Arena text_arena;
		Diagnostics text_diagnostics;
		Program *text_tree = parse(text_source, text_arena, text_diagnostics);
		std::string text_printed = print_ast(text_tree, text_source);
		*r_errors = text_diagnostics.format(text_source);
		size_t most = 0;
		size_t start = 0;
		while (start < text_printed.size()) {
			size_t indent = text_printed.find_first_not_of(' ', start) - start;
			most = indent > most ? indent : most;
			start = text_printed.find('\n', start) + 1;
		}
		return most / 2;
	};
	std::string long_sum = "1";
	for (int i = 0; i < 440; i++) {
		long_sum += "+1";
	}
	std::string named = "var x:int = " + long_sum + ";";
	for (int i = 0; i < 120; i++) {
		named = "function f" + std::to_string(i) + "():void { " + named + " }";
	}
	std::string named_errors;
	CHECK(depth_of(named, &named_errors) <= 600);
	CHECK_EQ(count(named_errors, "code is nested too deeply"), 1);
	std::string defaults = long_sum;
	for (int i = 0; i < 120; i++) {
		defaults = "function (a:* = " + defaults + "):void {}";
	}
	std::string default_errors;
	CHECK(depth_of("var g:Function = " + defaults + ";", &default_errors) <= 600);
	CHECK_EQ(count(default_errors, "code is nested too deeply"), 1);
	// Twenty named functions deep is ordinary code and must be accepted.
	std::string modest = "var x:int = 1 + 2;";
	for (int i = 0; i < 20; i++) {
		modest = "function f" + std::to_string(i) + "():void { " + modest + " }";
	}
	std::string modest_errors;
	CHECK(depth_of(modest, &modest_errors) > 40);
	CHECK_EQ(modest_errors, std::string(""));

	// Below the cap, nothing is reported.
	NO_ERRORS(expr(sum));
	NO_ERRORS(stmt("var g:Function = function():* { return function():* { return " + sum + "; }; };"));
}

static void ranges() {
	// (a + b) * c
	Source source("t.as", "(a + b) * c");
	Arena arena;
	Diagnostics diagnostics;
	Expr *product = parse_expression_for_test(source, arena, diagnostics);
	CHECK(product->kind == NodeKind::Binary);
	CHECK_EQ(product->offset, 0u);
	CHECK_EQ(product->end, 11u);
	Expr *sum = static_cast<Binary *>(product)->left;
	CHECK(sum->parenthesized);
	CHECK_EQ(sum->offset, 0u);
	CHECK_EQ(sum->end, 7u);
	Expr *c = static_cast<Binary *>(product)->right;
	CHECK(!c->parenthesized);
	CHECK_EQ(c->offset, 10u);
	CHECK_EQ(c->end, 11u);

	//                     0         1         2         3         4         5         6
	//                     0123456789012345678901234567890123456789012345678901234567890123456789
	Source file("t.as", "[Tag(a=\"x\", 5)] public class Foo { function bar(p:int = 1):void { break out; } }");
	Arena file_arena;
	Diagnostics file_diagnostics;
	Program *program = parse(file, file_arena, file_diagnostics);
	CHECK_EQ(file_diagnostics.format(file), std::string(""));
	ClassDecl *decl = static_cast<ClassDecl *>(program->definitions[0]);
	CHECK_EQ(decl->offset, 0u);
	CHECK_EQ(decl->name_offset, 29u);
	CHECK_EQ(decl->end, (uint32_t)file.text().size());
	CHECK_EQ(decl->metadata[0].offset, 0u);
	CHECK_EQ(decl->metadata[0].end, 15u);
	CHECK(decl->metadata[0].args[0].value_is_string);
	CHECK(!decl->metadata[0].args[1].value_is_string);
	FunctionDecl *bar = static_cast<FunctionDecl *>(decl->members[0]);
	CHECK_EQ(bar->offset, 35u);
	CHECK_EQ(bar->name_offset, 44u);
	CHECK_EQ(bar->parameters[0].offset, 48u);
	CHECK_EQ(bar->parameters[0].end, 57u);
	Break *jump = static_cast<Break *>(bar->body->statements[0]);
	CHECK_EQ(jump->label_offset, 72u);
	CHECK_EQ(std::string(file.text().substr(jump->label_offset, 3)), std::string("out"));

	Source typed("t.as", "var v:flash.utils.Dictionary = null, w:int;");
	Arena typed_arena;
	Diagnostics typed_diagnostics;
	Program *typed_program = parse(typed, typed_arena, typed_diagnostics);
	VarDecl *variable = static_cast<VarDecl *>(typed_program->definitions[0]);
	CHECK_EQ(variable->declarators[0].offset, 4u);
	CHECK_EQ(variable->declarators[0].end, 35u);
	CHECK_EQ(variable->declarators[1].end, 42u);
	TypeRef *type = variable->declarators[0].type;
	CHECK_EQ(type->name_offsets.count, 3u);
	CHECK_EQ(type->name_offsets[0], 6u);
	CHECK_EQ(type->name_offsets[2], 18u);
}

static void member_level_recovery() {
	// A bad metadata value is one mistake: no second error at the '['.
	Parsed tagged = program("class A {\n\t[A(z=!)] var d:int;\n\tvar e:int;\n}\n");
	CHECK_EQ(count(tagged.errors, "error:"), 1);
	CHECK(has(tagged.tree, "Declarator name=\"e\""));
	// A member starting with a contextual modifier is a place to resume.
	Parsed resumed = program("class A {\n\tprivate var x:int = 1 +;\n\toverride function g():void {}\n}\nclass B {}\n");
	CHECK_EQ(count(resumed.errors, "error:"), 1);
	CHECK(has(resumed.tree, "FunctionDecl name=\"g\""));
	CHECK(has(resumed.tree, "ClassDecl name=\"B\""));
	// A mistake inside a method body must not disturb the members and classes after it.
	Parsed body = program("class A {\n\tfunction f():void { o = {,}; }\n\tfunction g():void {}\n}\nclass B {}\n");
	CHECK_EQ(count(body.errors, "error:"), 1);
	CHECK(has(body.tree, "FunctionDecl name=\"g\""));
	CHECK(has(body.tree, "ClassDecl name=\"B\""));
	Parsed misspelt = program("class A {\n\tpublic funtion f():void { a(); }\n\tpublic function g():void {}\n}\n");
	CHECK_EQ(count(misspelt.errors, "error:"), 1);
	CHECK(has(misspelt.tree, "FunctionDecl name=\"g\""));
	CHECK_EQ(program("class A {\n\tfunction f():void {\n\t\tg();\n").errors,
			std::string("t.as:2:20: error: '{' is never closed\n"));
	CHECK_EQ(program("[Meta(").errors, std::string("t.as:1:6: error: '(' is never closed\n"));
}

static void unsupported_in_expressions() {
	const char *e4x = "E4X (XML syntax) is not supported";
	CHECK_EQ(count(expr("a..b").errors, e4x), 1);
	CHECK_EQ(count(expr("a.@b").errors, e4x), 1);
	CHECK_EQ(count(expr("@b").errors, e4x), 1);
	CHECK_EQ(count(expr("x = <a/>").errors, e4x), 1);
	CHECK_EQ(count(expr("a::b").errors, "namespaces are not supported"), 1);
	CHECK(has(expr("a..b").tree, "Member name=\"b\""));
	CHECK(has(expr("a::b").tree, "Name"));
}

// ---- Task 5: statements ----

static void var_forms() {
	Parsed two = stmt("var a:int = 1, b;");
	NO_ERRORS(two);
	CHECK_EQ(two.tree, std::string(R"(VarDecl
  Declarator name="a"
    Type int
    NumberLit text="1"
  Declarator name="b"
)"));
	CHECK(has(stmt("const K:Number = 2;").tree, "VarDecl const"));
	NO_ERRORS(stmt("var x:*=1;"));
	CHECK(has(stmt("var x:*=1;").tree, "Type *"));
	NO_ERRORS(stmt("var v:Vector.<Vector.<int>>= null;"));
}

static void if_else_chain() {
	Parsed result = stmt("if (a) b(); else if (c) d(); else e();");
	NO_ERRORS(result);
	CHECK_EQ(result.tree, std::string(R"(If
  Name text="a"
  ExprStmt
    Call
      Name text="b"
  else:
    If
      Name text="c"
      ExprStmt
        Call
          Name text="d"
      else:
        ExprStmt
          Call
            Name text="e"
)"));
}

static void loops() {
	Parsed counted = stmt("for (var i:int = 0; i < n; i++) {}");
	NO_ERRORS(counted);
	CHECK(has(counted.tree, "  init:\n    VarDecl\n"));
	CHECK(has(counted.tree, "  condition:\n    Binary op=\"<\"\n"));
	CHECK(has(counted.tree, "  update:\n    Postfix op=\"++\"\n"));
	CHECK_EQ(stmt("for (;;) {}").tree, std::string("For\n  Block\n"));
	Parsed declared = stmt("for (var k:String in o) {}");
	NO_ERRORS(declared);
	CHECK_EQ(declared.tree, std::string(R"(ForIn
  VarDecl
    Declarator name="k"
      Type String
  Name text="o"
  Block
)"));
	CHECK_EQ(stmt("for (k in o) {}").tree, std::string("ForIn\n  Name text=\"k\"\n  Name text=\"o\"\n  Block\n"));
	Parsed each = stmt("for each (var v:* in list) {}");
	NO_ERRORS(each);
	CHECK(has(each.tree, "ForIn each\n"));
	NO_ERRORS(stmt("for each (item in items) total += item;"));
	NO_ERRORS(stmt("while (a) b();"));
	NO_ERRORS(stmt("do { } while (a);"));
	NO_ERRORS(stmt("for (i = 0, j = 1; i < j; i++, j--) {}"));
	CHECK_EQ(stmt("for (1 in o) {}").errors, std::string("t.as:1:6: error: invalid left-hand side in for-in\n"));
}

static void in_operator_and_for_headers() {
	Parsed assign = stmt("x = k in o;");
	NO_ERRORS(assign);
	CHECK(has(assign.tree, "Binary op=\"in\""));
	Parsed parenthesised = stmt("for (var i:int = (a in b) ? 1 : 0; ;) {}");
	NO_ERRORS(parenthesised);
	CHECK(has(parenthesised.tree, "For\n"));
	NO_ERRORS(stmt("for (var i:int = [a in b].length; ;) {}"));
	NO_ERRORS(stmt("for (var i:int = f(a in b); ;) {}"));
	NO_ERRORS(stmt("for (var o:Object = { k: a in b }; ;) {}"));
	NO_ERRORS(stmt("for (var f:Function = function ():Boolean { return a in b; }; ;) {}"));
}

static void switch_cases() {
	Parsed result = stmt("switch (x) { case 1: a(); break; case 2: default: b(); }");
	NO_ERRORS(result);
	CHECK_EQ(count(result.tree, "  Case\n"), 2);
	CHECK_EQ(count(result.tree, "  Default\n"), 1);
	CHECK(has(result.tree, "  Case\n    NumberLit text=\"2\"\n  Default\n"));
	CHECK_EQ(stmt("switch (x) { default: a(); default: b(); }").errors,
			std::string("t.as:1:28: error: more than one 'default' in switch\n"));
}

static void labels_and_jumps() {
	Parsed result = stmt("outer: for (;;) { continue outer; }");
	NO_ERRORS(result);
	CHECK(has(result.tree, "Labeled label=\"outer\""));
	CHECK(has(result.tree, "Continue label=\"outer\""));
	CHECK(has(stmt("{ break; }").tree, "Break\n"));
}

static void try_forms() {
	Parsed full = stmt("try { } catch (e:Error) { } finally { }");
	NO_ERRORS(full);
	CHECK_EQ(full.tree, std::string(R"(Try
  Block
  Catch name="e"
    Type Error
    Block
  finally:
    Block
)"));
	NO_ERRORS(stmt("try { } catch (e) { }"));
	CHECK_EQ(stmt("try { }").errors, std::string("t.as:1:1: error: 'try' needs a 'catch' or 'finally'\n"));
	CHECK_EQ(stmt("{ throw\nx; }").errors, std::string("t.as:1:3: error: expected an expression after 'throw'\n"));
}

static void closures() {
	Parsed result = stmt("var f:Function = function (a:int, b:int = 2, ...rest):void { return; };");
	NO_ERRORS(result);
	CHECK_EQ(count(result.tree, "Parameter name="), 3);
	CHECK(has(result.tree, "Parameter name=\"rest\" rest"));
	CHECK(has(result.tree, "returns:\n          Type void"));
	Parsed nested = stmt("{ function inner():void {} }");
	NO_ERRORS(nested);
	CHECK(has(nested.tree, "FunctionStmt\n    FunctionDecl name=\"inner\""));
	NO_ERRORS(stmt("{ function get():int { return 1; } }"));
	CHECK(has(stmt("{ function get():int { return 1; } }").tree, "FunctionDecl name=\"get\"\n"));
	NO_ERRORS(stmt("var g:Function = function named(a:*=null):* { return a; };"));
	CHECK_EQ(stmt("var h:Function = function (...a, b) {};").errors,
			std::string("t.as:1:28: error: a rest parameter must be last\n"));
}

static void line_breaks_end_statements() {
	Parsed three = stmt("{\nvar a = 1\nvar b = 2\na = b\n}");
	NO_ERRORS(three);
	CHECK_EQ(count(three.tree, "\n  "), 3 + count(three.tree, "\n    "));
	CHECK_EQ(count(three.tree, "  VarDecl\n"), 2);
	CHECK_EQ(count(three.tree, "  ExprStmt\n"), 1);
	Parsed returned = stmt("{\nreturn\nx;\n}");
	NO_ERRORS(returned);
	CHECK_EQ(returned.tree, std::string("Block\n  Return\n  ExprStmt\n    Name text=\"x\"\n"));
	Parsed call = stmt("{\na = b\n(c)\n}");
	NO_ERRORS(call);
	CHECK_EQ(count(call.tree, "ExprStmt"), 1);
	CHECK(has(call.tree, "Call"));
	Parsed index = stmt("{\na = b\n[0]\n}");
	CHECK_EQ(count(index.tree, "ExprStmt"), 1);
	CHECK(has(index.tree, "Index"));
	Parsed increments = stmt("{\na\n++b\n}");
	NO_ERRORS(increments);
	CHECK_EQ(count(increments.tree, "ExprStmt"), 2);
}

static void statement_errors_and_recovery() {
	Parsed missing = stmt("{ a = 1 b = 2 }");
	CHECK_EQ(missing.errors, std::string("t.as:1:9: error: expected ';' but found 'b'\n"));
	CHECK_EQ(count(missing.tree, "ExprStmt"), 2);
	Parsed recovered = stmt("{ var = 1;\n ok();\n x = ;\n}");
	CHECK_EQ(count(recovered.errors, "error:"), 2);
	CHECK(has(recovered.errors, "t.as:1:"));
	CHECK(has(recovered.errors, "t.as:3:"));
	CHECK(has(recovered.tree, "Name text=\"ok\""));
	CHECK_EQ(stmt("{ a();").errors, std::string("t.as:1:1: error: '{' is never closed\n"));
	// Only the innermost unclosed construct is reported; the rest follow from it.
	CHECK_EQ(stmt("{ if (a) { f(1, ").errors, std::string("t.as:1:13: error: '(' is never closed\n"));
	CHECK_EQ(stmt("{ try {").errors, std::string("t.as:1:7: error: '{' is never closed\n"));
	CHECK_EQ(stmt("{ var s = \"abc").errors, std::string("t.as:1:11: error: unterminated string\n"));
	CHECK_EQ(stmt("{ a(); /* x").errors, std::string("t.as:1:8: error: unterminated comment\n"));
	// One mistake inside nested braces must not end the enclosing block early.
	Parsed literal = stmt("{ o = {,}; ok(); }");
	CHECK_EQ(count(literal.errors, "error:"), 1);
	CHECK(has(literal.tree, "Name text=\"ok\""));
	Parsed header = stmt("{ for (;;;) { a(); } ok(); }");
	CHECK_EQ(count(header.errors, "error:"), 1);
	CHECK(has(header.tree, "Name text=\"ok\""));
	Parsed fields = stmt("{ o = {a:1 b:2}; ok(); }");
	CHECK_EQ(count(fields.errors, "error:"), 1);
	CHECK(has(fields.tree, "Name text=\"ok\""));
	CHECK_EQ(stmt("{ var s = \"abc\nvar t = 1; }").errors, std::string("t.as:1:11: error: unterminated string\n"));
}

static void unsupported_statements() {
	CHECK_EQ(stmt("with (o) { a(); }").errors, std::string("t.as:1:1: error: the 'with' statement is not supported\n"));
	CHECK_EQ(stmt("{ use namespace mx_internal; a(); }").errors,
			std::string("t.as:1:3: error: namespaces are not supported\n"));
	CHECK_EQ(stmt("{ include \"x.as\"; a(); }").errors, std::string("t.as:1:3: error: 'include' is not supported\n"));
	CHECK_EQ(stmt("{ default xml namespace = ns; a(); }").errors,
			std::string("t.as:1:3: error: E4X (XML syntax) is not supported\n"));
	CHECK(has(stmt("{ include \"x.as\"; a(); }").tree, "Name text=\"a\""));
	Parsed imported = stmt("{ import flash.utils.getTimer; a(); }");
	NO_ERRORS(imported);
	CHECK(has(imported.tree, "Import name=\"flash.utils.getTimer\""));
}

// ---- Task 6: declarations, packages, parse() ----

static void empty_file() {
	Parsed result = program("");
	NO_ERRORS(result);
	CHECK_EQ(result.tree, std::string("Program\n"));
	NO_ERRORS(program("// just a comment\n"));
}

static void package_with_class() {
	Parsed result = program("package game.actors {\n\timport godot.Node2D;\n\n"
							"\tpublic class Player extends Node2D implements IHit, IMove {\n\t}\n}\n");
	NO_ERRORS(result);
	CHECK_EQ(result.tree, std::string(R"(Program
  PackageDecl name="game.actors"
    Import name="godot.Node2D"
    ClassDecl name="Player" mods=public
      extends:
        Type Node2D
      implements:
        Type IHit
        Type IMove
)"));
	NO_ERRORS(program("package { class A {} }"));
	CHECK(has(program("package { class A {} }").tree, "  PackageDecl\n    ClassDecl name=\"A\"\n"));
}

static void members() {
	Parsed result = program(R"(package {
	public class Unit {
		[Export] public var speed:Number = 200;
		private static const MAX:int = 3;
		override public function _process(delta:Number):void { }
		public function get hp():int { return _hp; }
		public function set hp(v:int):void { }
		function get():int { return 0; }
		public native function fast(x:int):int;
		internal final function done():void {}
	}
})");
	NO_ERRORS(result);
	CHECK(has(result.tree, "VarDecl mods=public\n        Metadata name=\"Export\"\n        Declarator name=\"speed\""));
	CHECK(has(result.tree, "VarDecl const mods=private,static\n"));
	CHECK(has(result.tree, "FunctionDecl name=\"_process\" mods=public,override\n"));
	CHECK(has(result.tree, "FunctionDecl name=\"hp\" getter mods=public\n"));
	CHECK(has(result.tree, "FunctionDecl name=\"hp\" setter mods=public\n"));
	CHECK(has(result.tree, "FunctionDecl name=\"get\"\n"));
	CHECK(has(result.tree, "FunctionDecl name=\"fast\" mods=public,native\n"));
	CHECK(has(result.tree, "FunctionDecl name=\"done\" mods=internal,final\n"));
	CHECK_EQ(program("class A { function f():void }").errors,
			std::string("t.as:1:29: error: expected '{' but found '}'\n"));
}

static void metadata_forms() {
	Parsed result = program(R"([Signal(name="hit", args="amount:int")]
[Tool]
[Embed(source="a.png", mimeType="image/png")]
[Foo(1, bar)]
class A {}
)");
	NO_ERRORS(result);
	CHECK(has(result.tree, "Metadata name=\"Signal\" args=[name=\"hit\", args=\"amount:int\"]\n"));
	CHECK(has(result.tree, "Metadata name=\"Tool\"\n"));
	CHECK(has(result.tree, "Metadata name=\"Embed\" args=[source=\"a.png\", mimeType=\"image/png\"]\n"));
	CHECK(has(result.tree, "Metadata name=\"Foo\" args=[\"1\", \"bar\"]\n"));
}

static void metadata_after_initializer_without_semicolon() {
	Parsed result = program("class A {\n\tpublic var a:int = 1\n\t[Export]\n\tpublic var b:int = 2\n}\n");
	NO_ERRORS(result);
	CHECK_EQ(count(result.tree, "VarDecl mods=public"), 2);
	CHECK(has(result.tree, "Metadata name=\"Export\"\n      Declarator name=\"b\""));
	Parsed top = program("var a:Array = b\n[Export]\nvar c:int = 1\n");
	NO_ERRORS(top);
	CHECK_EQ(count(top.tree, "VarDecl"), 2);
	// Inside a function body the bracket still continues the expression.
	CHECK(has(program("function f():void {\n\tvar a:* = b\n\t[0]\n}\n").tree, "Index"));
}

static void interface_forms() {
	Parsed result = program("interface IHit extends IA, IB { function hit(n:int):void; function get hp():int; }");
	NO_ERRORS(result);
	CHECK_EQ(result.tree, std::string(R"(Program
  InterfaceDecl name="IHit"
    extends:
      Type IA
      Type IB
    FunctionDecl name="hit"
      Parameter name="n"
        Type int
      returns:
        Type void
    FunctionDecl name="hp" getter
      returns:
        Type int
)"));
	CHECK_EQ(program("interface I { function f():void {} }").errors,
			std::string("t.as:1:33: error: interface methods cannot have a body\n"));
	CHECK_EQ(program("interface I { public function f():void; }").errors,
			std::string("t.as:1:15: error: interface methods cannot have modifiers\n"));
	NO_ERRORS(program("interface I { function a():void\n function b():void\n }"));
}

static void helpers_outside_package() {
	Parsed result = program("package { public class Main {} }\nclass Helper { }\nfunction util():void { }\n");
	NO_ERRORS(result);
	CHECK_EQ(result.tree, std::string(R"(Program
  PackageDecl
    ClassDecl name="Main" mods=public
  ClassDecl name="Helper"
  FunctionDecl name="util"
    returns:
      Type void
    Block
)"));
}

static void imports() {
	CHECK(has(program("import flash.utils.*;").tree, "Import name=\"flash.utils\" wildcard\n"));
	Parsed in_class = program("class A { import flash.utils.Dictionary; var d:Dictionary; }");
	NO_ERRORS(in_class);
	CHECK(has(in_class.tree, "    Import name=\"flash.utils.Dictionary\"\n"));
	NO_ERRORS(program("package a { import b.C\n import d.E\n class F {} }"));
}

static void modifier_errors() {
	CHECK_EQ(program("public public class A {}").errors, std::string("t.as:1:8: error: duplicate 'public'\n"));
	CHECK_EQ(program("public private class A {}").errors,
			std::string("t.as:1:8: error: only one access modifier is allowed\n"));
	CHECK_EQ(program("class A { static static var x:int; }").errors, std::string("t.as:1:18: error: duplicate 'static'\n"));
	NO_ERRORS(program("public dynamic class A {}"));
	CHECK(has(program("public dynamic class A {}").tree, "mods=public,dynamic"));
}

static void top_level_errors() {
	Parsed stray = program("x = 1;\nclass A {}\n");
	CHECK_EQ(stray.errors, std::string("t.as:1:1: error: expected a class, interface, function or variable definition\n"));
	CHECK(has(stray.tree, "ClassDecl name=\"A\""));
	Parsed twice = program("package a { }\npackage b { }\n");
	CHECK_EQ(twice.errors, std::string("t.as:2:1: error: only one package block is allowed in a file\n"));
	Parsed member = program("class A {\n\tx = 1;\n\tvar y:int;\n}\n");
	CHECK_EQ(member.errors, std::string("t.as:2:2: error: expected a member definition\n"));
	CHECK(has(member.tree, "Declarator name=\"y\""));
}

static void unsupported_definitions() {
	struct Case {
		const char *text;
		const char *message;
	};
	const Case cases[] = {
		{ "namespace mx_internal;\nclass A {}", "t.as:1:1: error: namespaces are not supported\n" },
		{ "namespace ns = \"http://x\";\nclass A {}", "t.as:1:1: error: namespaces are not supported\n" },
		{ "use namespace mx_internal;\nclass A {}", "t.as:1:1: error: namespaces are not supported\n" },
		{ "class A { arcane function f():void {} }", "t.as:1:11: error: namespaces are not supported\n" },
		{ "class A { my_ns static var x:int; }", "t.as:1:11: error: namespaces are not supported\n" },
		{ "class A { public static my_ns function f():void {} }", "t.as:1:25: error: namespaces are not supported\n" },
		{ "class A { oops; }", "t.as:1:11: error: expected a member definition\n" },
		{ "include \"other.as\";\nclass A {}", "t.as:1:1: error: 'include' is not supported\n" },
		{ "default xml namespace = ns;\nclass A {}", "t.as:1:1: error: E4X (XML syntax) is not supported\n" },
		{ "class B { function f():void { with (o) { g(); } } }\nclass A {}",
				"t.as:1:31: error: the 'with' statement is not supported\n" },
		{ "class B { function f():void { var x:* = a::b; } }\nclass A {}",
				"t.as:1:42: error: namespaces are not supported\n" },
		{ "class B { var x:XML = <a/>; }\nclass A {}", "t.as:1:23: error: E4X (XML syntax) is not supported\n" },
		{ "class B { function f():void { var x:* = a..b + a.@c; } }\nclass A {}",
				"t.as:1:42: error: E4X (XML syntax) is not supported\nt.as:1:50: error: E4X (XML syntax) is not supported\n" },
	};
	for (const Case &item : cases) {
		Parsed result = program(item.text);
		CHECK_EQ(result.errors, std::string(item.message));
		CHECK(has(result.tree, "ClassDecl name=\"A\""));
	}
}

static void whole_program_never_crashes() {
	std::string braces(5000, '{');
	std::string control("\x00\x01\x02", 3);
	for (const std::string &text : { std::string("package {"), std::string("class"), std::string("class A { function"),
				 std::string("class A { var x:Vector.<"), std::string("["), std::string("}}}}"), braces, control }) {
		Parsed result = program(text);
		CHECK(!result.errors.empty());
		CHECK(!result.tree.empty());
	}
}

static const char *const CORPUS[] = { "Alloc", "ArrayConversion", "Empty", "MoveNodes", "NBody", "PerNodeChild",
	"PerNodeProcess", "Probe", "RayQueries", "Saxpy", "Shapes", "Sieve", "SignalTraffic", "V2Probe" };

static void corpus_parses_cleanly() {
	for (const char *name : CORPUS) {
		std::string path = std::string(KAS3_TESTS_DIR) + "/corpus/" + name + ".as";
		std::string text;
		CHECK(read_file(path, &text));
		Source source(path, text);
		Arena arena;
		Diagnostics diagnostics;
		Program *tree = parse(source, arena, diagnostics);
		CHECK(tree != nullptr);
		CHECK_EQ(diagnostics.format(source), std::string(""));
		CHECK(tree->package != nullptr || tree->definitions.count > 0);
	}
}

// Every prefix of every corpus file is a malformed program. Parsing each must finish and return a tree.
static void every_prefix_of_the_corpus_parses() {
	int parsed = 0;
	bool all_returned = true;
	for (const char *name : CORPUS) {
		std::string text;
		if (!read_file(std::string(KAS3_TESTS_DIR) + "/corpus/" + name + ".as", &text)) {
			continue;
		}
		for (size_t length = 0; length <= text.size(); length++) {
			Source source("prefix.as", text.substr(0, length));
			Arena arena;
			Diagnostics diagnostics;
			Program *tree = parse(source, arena, diagnostics);
			all_returned = all_returned && tree != nullptr;
			// Printing walks the whole tree, which checks that it is well formed.
			all_returned = all_returned && !print_ast(tree, source).empty();
			parsed++;
		}
	}
	CHECK(all_returned);
	CHECK(parsed > 10000);
}

// Every corpus file with one byte removed: mistakes in the middle of a file, which exercise recovery.
static void every_single_byte_deletion_of_the_corpus_parses() {
	int parsed = 0;
	bool all_returned = true;
	for (const char *name : CORPUS) {
		std::string text;
		if (!read_file(std::string(KAS3_TESTS_DIR) + "/corpus/" + name + ".as", &text)) {
			continue;
		}
		for (size_t index = 0; index < text.size(); index++) {
			std::string damaged = text;
			damaged.erase(index, 1);
			Source source("damaged.as", damaged);
			Arena arena;
			Diagnostics diagnostics;
			Program *tree = parse(source, arena, diagnostics);
			all_returned = all_returned && tree != nullptr && !print_ast(tree, source).empty();
			parsed++;
		}
	}
	CHECK(all_returned);
	CHECK(parsed > 10000);
}

int main() {
	empty_file();
	package_with_class();
	members();
	metadata_forms();
	metadata_after_initializer_without_semicolon();
	interface_forms();
	helpers_outside_package();
	imports();
	modifier_errors();
	top_level_errors();
	unsupported_definitions();
	member_level_recovery();
	whole_program_never_crashes();
	corpus_parses_cleanly();
	every_prefix_of_the_corpus_parses();
	every_single_byte_deletion_of_the_corpus_parses();
	precedence();
	calls_and_members();
	reserved_word_after_dot();
	new_forms();
	nested_type_arguments();
	literals();
	regex_where_an_expression_starts();
	postfix_needs_same_line();
	expression_errors();
	depth_and_length_caps();
	deep_and_long_inputs_do_not_crash();
	ranges();
	unsupported_in_expressions();
	var_forms();
	if_else_chain();
	loops();
	in_operator_and_for_headers();
	switch_cases();
	labels_and_jumps();
	try_forms();
	closures();
	line_breaks_end_statements();
	statement_errors_and_recovery();
	unsupported_statements();
	return check::finish();
}
