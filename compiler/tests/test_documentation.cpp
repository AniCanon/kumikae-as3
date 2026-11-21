#include "query_completion.h"

using namespace completion_test;

static void test_keyword_completions() {
	const auto body = complete("package { class Player { function f():void { | } } }");
	const auto *returns = body.find(token_kind_spelling(TokenKind::KwReturn));
	CHECK(returns && returns->is_keyword && returns->documentation.find("current function") != std::string::npos);
	const auto *loop = body.find("for each");
	CHECK(loop && loop->is_keyword && loop->edit && loop->documentation.find("values") != std::string::npos);
	CHECK(!complete("package { class Player { fo| } }").has("for each"));
	for (const char *name : { "get", "set" }) {
		const auto members = complete(std::string("package { class Player { public function ") + name[0] + "| } }");
		const auto *accessor = members.find(name);
		CHECK(accessor && accessor->is_keyword && accessor->documentation.find("accessor") != std::string::npos);
		CHECK(!body.has(name));
	}
}

int main() {
	test_keyword_completions();
	const auto inherited = complete("package { class Player extends Base { function f():void { this.val| } } }",
			"package { public class Base { public function get value():int { return 1; }"
			"/** **Number of tiles.**\r\n *\r\n *     trace(value);\r\n */\u00a0public function set value(n:int):void {} } }");
	const auto *property = inherited.find("value");
	CHECK(property && property->documentation == "**Number of tiles.**\n\n    trace(value);");

	// Split-file partial override: subclass overrides setter only, getter ASDoc lives in Base's unit.
	const auto partial = complete(
			"package { class Player extends Base { override public function set count(n:int):void {} function f():void { this.cou| } } }",
			"package { public class Base { /** Base getter docs. */ public function get count():int { return 0; }"
			" public function set count(n:int):void {} } }");
	const auto *partial_prop = partial.find("count");
	CHECK(partial_prop && partial_prop->documentation == "Base getter docs.");

	for (const char *source : {
		"package { | }", "package { class Player { | } }", "package { class Player { function run():void { | } } }",
	}) {
		const auto list = complete(source);
		bool found = false;
		for (const auto &item : list.items) {
			if (item.is_keyword) {
				found = true;
				CHECK(!item.documentation.empty());
			}
		}
		CHECK(found);
	}
	return check::finish();
}
