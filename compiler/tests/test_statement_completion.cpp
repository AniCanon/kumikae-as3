#include "query_completion.h"

using namespace completion_test;

static const std::string HEAD = "package { public class Player { function run():void { ";
static const std::string TAIL = " } } }";

static void test_blocks() {
	for (const auto &[prefix, name] : std::vector<std::pair<std::string, std::string>>{
		{ "tr", "try" }, { "try", "try" }, { "i", "if" }, { "if", "if" },
		{ "wh", "while" }, { "while", "while" }, { "d", "do" }, { "do", "do" },
		{ "fo", "for" }, { "for", "for" }, { "fo", "for each" }, { "sw", "switch" }, { "switch", "switch" } }) {
		const std::string source = HEAD + "var condition:Boolean = true; var count:int = 2; "
				"var items:Array = []; var value:int = 1; " + prefix + "|" + TAIL;
		const auto offered = complete(source);
		const auto *item = offered.find(name);
		CHECK(item && item->edit);
		if (!item || !item->edit) {
			continue;
		}
		CHECK(item->edit->text.find('{') != std::string::npos);
		CHECK(!item->tab_stops.empty());
		uint32_t previous = 0;
		std::unordered_map<uint32_t, std::string> defaults;
		for (const auto &stop : item->tab_stops) {
			CHECK(previous <= stop.begin && stop.begin <= stop.end && stop.end <= item->edit->text.size());
			const auto text = item->edit->text.substr(stop.begin, stop.end - stop.begin);
			const auto [entry, inserted] = defaults.emplace(stop.index, text);
			CHECK(inserted || entry->second == text);
			previous = stop.end;
		}
		CHECK_EQ(item->tab_stops.back().index, 0u);
		CHECK_EQ(item->tab_stops.back().begin, item->edit->text.size());
		Compilation fresh;
		fresh.add("Player.as", apply_completion(source, name));
		fresh.check();
		CHECK_EQ(fresh.format_diagnostics(), std::string());
	}
}

static void test_try_context() {
	for (const auto &body : { "tr|", "try|", "|", "trace(1); |", "if (true) {} |", "if (true) tr|", "while (true) tr|", "label: tr|",
		"if (true) {} tr|", "if (true) /* gap */ tr|", "while (true) // gap\ntr|",
		"trace(1); /* comment */ tr|", "trace(1)\ntr|",
		"switch (1) { case 1: tr| }", "var f:Function = function():void { tr| };" }) {
		const auto offered = complete(HEAD + body + TAIL);
		const auto *item = offered.find("try");
		CHECK(item && item->edit);
	}
	for (const auto &body : { "trace(tr|);", "return tr|", "return |", "throw |", "var x:* = |", "var x:* = tr|;", "var x:* = { tr| };",
		"for (tr|; ; ) {}", "for (; tr|; ) {}", "if (tr|) {}", "var x:* = [tr|];",
		"trace(1 + tr|);", "tr|y {} catch (e:Error) {}", "tr| /* gap */ {} catch(e:Error) {}",
		"trace(try|);", "return try|", "var x:* = try|;", "for (try|; ; ) {}", "if (try|) {}",
		"while (try|) {}", "for (; try|; ) {}", "for (; ; try|) {}", "for each (var x:* in try|) {}",
		"var f:Function = function(x:* = tr|):void {};", "// tr|\n", "var s:String = 'tr|';", "var r:RegExp = /tr|/;" }) {
		const auto offered = complete(HEAD + body + TAIL);
		const auto *item = offered.find("try");
		CHECK_EQ(item && item->edit ? std::string(body) : std::string(), std::string());
	}
	CHECK(!complete("package { public class Player { tr| } }").has("try"));
	CHECK(!complete("package { tr| }").has("try"));
}

static void test_layout_and_range() {
	const std::string source = "package {\r\n\tpublic class Player {\r\n\t\tfunction run():void {\r\n\t\t\ttr|suffix\r\n\t\t}\r\n\t}\r\n}";
	const auto offered = complete(source);
	const auto *item = offered.find("try");
	CHECK(item && item->edit);
	if (item && item->edit) {
		CHECK_EQ(item->edit->text, std::string("try {\r\n\t\t\t\t\r\n\t\t\t} catch (error:Error) {\r\n\t\t\t\tthrow error;\r\n\t\t\t}"));
		CHECK_EQ(item->edit->end - item->edit->begin, 8u);
	}
}

int main() {
	test_blocks();
	test_try_context();
	test_layout_and_range();
	return check::finish();
}
