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

#ifdef KAS3_HAVE_PROGRAM
static Parsed program(const std::string &p_text) {
	Source source("t.as", p_text);
	Arena arena;
	Diagnostics diagnostics;
	Program *tree = parse(source, arena, diagnostics);
	CHECK(tree != nullptr);
	return Parsed{ strip_positions(print_ast(tree, source)), diagnostics.format(source) };
}
#endif

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
	CHECK_EQ(expr("(1 + 2").errors, std::string("t.as:1:7: error: expected ')' but found end of file\n"));
	CHECK_EQ(expr("f(1, 2").errors, std::string("t.as:1:7: error: expected ')' but found end of file\n"));
	CHECK_EQ(expr("a.").errors, std::string("t.as:1:3: error: expected a property name but found end of file\n"));
	CHECK_EQ(expr("x = 12abc").errors, std::string("t.as:1:5: error: invalid number\n"));
}

static void depth_and_length_caps() {
	std::string nested(300, '(');
	Parsed deep = expr(nested);
	CHECK_EQ(count(deep.errors, "code is nested too deeply"), 1);
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
	for (int i = 0; i < 4000; i++) {
		fine += "+1";
	}
	NO_ERRORS(expr(fine));
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
	CHECK_EQ(stmt("{ a();").errors, std::string("t.as:1:7: error: expected '}' but found end of file\n"));
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

int main() {
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
