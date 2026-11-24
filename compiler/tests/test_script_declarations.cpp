#include "check.h"
#include "../godot_ext/script_declarations.h"

using namespace kas3;

using Member = ScriptDeclarations::Member;

int main() {
	ScriptDeclarations declarations;
	size_t walks = 0;
	auto fields = [&](auto &r_names) { ++walks; r_names.emplace("score", Member::Field); };
	for (int hit = 0; hit < 10; ++hit) {
		CHECK(declarations.lookup(12, "score", fields) == Member::Field);
		CHECK(declarations.lookup(12, "missing", fields) == Member::Missing);
	}
	CHECK_EQ(walks, size_t(1));
	CHECK(declarations.lookup(13, "score", [](auto &r_names) { r_names.emplace("replacement", Member::Method); }) == Member::Missing);
	declarations.clear();
	CHECK(declarations.lookup(12, "score", [](auto &r_names) { r_names.emplace("renamed", Member::Field); }) == Member::Missing);
	declarations.clear();
	walks = 0;
	for (size_t i = 0; i < 65 * 20; ++i) {
		CHECK(declarations.lookup(i % 65, "score", fields) == Member::Field);
	}
	CHECK(walks < 160);
	std::printf("65 rotating scripts: %zu reflections in 1300 reads\n", walks);
	return check::finish();
}
