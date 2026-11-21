#include "query_completion.h"

using namespace completion_test;

int main() {
	const auto inherited = complete("package { class Player extends Base { function f():void { this.val| } } }",
			"package { public class Base { public function get value():int { return 1; }"
			"/** **Number of tiles.**\r\n *\r\n *     trace(value);\r\n */\u00a0public function set value(n:int):void {} } }");
	const auto *property = inherited.find("value");
	CHECK(property && property->documentation == "**Number of tiles.**\n\n    trace(value);");

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
