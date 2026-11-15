#include "query_completion.h"

using namespace kas3;
using namespace completion_test;

static void accepts(const std::string &p_source, const char *p_base, const char *p_name, const char *p_signature) {
	const auto offered = complete(p_source, p_base);
	if (offered.count(p_name) != 1) {
		std::printf("Expected %s from %s with %s\n", p_name, p_source.c_str(), p_base);
	}
	CHECK_EQ(offered.count(p_name), size_t{1});
	if (!offered.has(p_name)) {
		return;
	}
	const auto applied = apply_completion(p_source, p_name, p_base);
	CHECK(applied.find(p_signature) != std::string::npos);
	Compilation clean;
	clean.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	clean.add("Other.as", p_base);
	clean.add("Player.as", applied);
	clean.check();
	CHECK_EQ(clean.format_diagnostics(), std::string());
}

static void qualified_defaults() {
	const char *child = "package child { public class Player extends a.b.Other { override function st| } }";
	for (const char *value : { "VALUE", "Other.VALUE", "a.b.Other.VALUE" }) {
		const auto base = std::string("package a.b { public class Other { public static const VALUE:int=7; ") +
				"public function step(n:int=" + value + "):void {} } }";
		accepts(child, base.c_str(), "step", "step(n:int = a.b.Other.VALUE):void");
	}
	accepts(child, "package a.b { import godot.Vector2; public class Other { "
			"public function step(n:Vector2=Vector2.ZERO):void {} } }", "step", "n:godot.Vector2 = godot.Vector2.ZERO");
	accepts(child, "package a.b { public class Other { public static const VALUE:int=7; "
			"public function step(n:int=Other.VALUE + a.b.Other.VALUE):void {} } }",
			"step", "n:int = a.b.Other.VALUE + a.b.Other.VALUE");
	for (const char *value : { "VALUE", "Other.VALUE", "a.b.Other.VALUE" }) {
		const auto base = std::string("package a.b { public class Other { private static const VALUE:int=7; ") +
				"public function step(n:int=" + value + "):void {} } }";
		CHECK(!complete(child, base.c_str()).has("step"));
	}
	CHECK(!complete(child, "package a.b { internal class Hidden { public static const VALUE:int=7; } "
			"public class Other { public function step(n:int=a.b.Hidden.VALUE):void {} } }").has("step"));
}

static void accessor_slots() {
	const char *base = "package { public class Other { public function get count():int { return 1; } "
			"public function set count(v:int):void {} } }";
	for (const char *prefix : { "", "cou", "count" }) {
		accepts(std::string("package { public class Player extends Other { ") +
				"public override function get count():int { return 2; } override function set " + prefix + "| } }",
				base, "count", "set count(v:int):void");
		accepts(std::string("package { public class Player extends Other { ") +
				"public override function set count(v:int):void {} override function get " + prefix + "| } }",
				base, "count", "get count():int");
	}
	CHECK(!complete("package { public class Player extends Other { "
			"public override function set count(v:int):void {} override function set cou| } }", base).has("count"));
	for (const char *members : {
		"public function get count():int { return 1; } public final function set count(v:int):void {}",
		"public final function set count(v:int):void {} public function get count():int { return 1; }",
	}) {
		const auto final_base = std::string("package { public class Other { ") + members + " } }";
		CHECK(!complete("package { public class Player extends Other { override function set cou| } }", final_base.c_str()).has("count"));
		accepts("package { public class Player extends Other { override function get cou| } }",
				final_base.c_str(), "count", "get count():int");
	}
	for (const char *members : {
		"public final function get count():int { return 1; } public function set count(v:int):void {}",
		"public function set count(v:int):void {} public final function get count():int { return 1; }",
	}) {
		const auto final_base = std::string("package { public class Other { ") + members + " } }";
		CHECK(!complete("package { public class Player extends Other { override function get cou| } }", final_base.c_str()).has("count"));
		accepts("package { public class Player extends Other { override function set cou| } }",
				final_base.c_str(), "count", "set count(v:int):void");
	}
}

static void inherited_slots() {
	accepts("package { public class Player extends Other { override function set cou| } }",
			"package { public class Other extends Grand { public override function get count():int { return 2; } } } "
			"class Grand { public function get count():int { return 1; } public function set count(v:int):void {} }",
			"count", "set count(v:int):void");
	CHECK(complete("package { public class Player extends Other { override function st| } }",
			"package { public class Other extends Grand { private function step():void {} } } "
			"class Grand { public function step():void {} }").has("step"));
	// A visible non-method still shadows the name, even in an incomplete/error-containing program.
	CHECK(!complete("package { public class Player extends Other { override function st| } }",
			"package { public class Other extends Grand { public var step:int; } } "
			"class Grand { public function step():void {} }").has("step"));
	accepts("package { public class Player extends Other { override function | } }",
			"package { public class Other { public function step():void {} } }", "step", "step():void");
	accepts("package { public class Player extends Other { override function ge| } }",
			"package { public class Other { public function get():int { return 1; } } }", "get", "get():int");
	CHECK(!complete("package { public class Player extends Other { override function ge|t count } }",
			"package { public class Other { public function get count():int { return 1; } } }").has("count"));
}

int main() {
	qualified_defaults();
	accessor_slots();
	inherited_slots();
	return check::finish();
}
