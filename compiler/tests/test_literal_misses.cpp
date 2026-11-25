#include "check.h"
#include "../godot_ext/literal_misses.h"

using namespace kas3;

int main() {
	LiteralMisses misses;
	int walks = 0;
	auto walk = [&] { ++walks; return &vm::MISSING_MEMBER; };
	for (int i = 0; i < 256; i++) {
		misses.lookup("slot" + std::to_string(i), vm::NameSource::Computed, walk);
	}
	misses.lookup("slot0", vm::NameSource::Computed, walk);
	CHECK_EQ(walks, 257);
	misses.lookup("score", vm::NameSource::Literal, walk);
	misses.lookup("score", vm::NameSource::Literal, walk);
	misses.lookup("score", vm::NameSource::Computed, walk);
	CHECK_EQ(walks, 258);
	misses.clear();
	misses.lookup("score", vm::NameSource::Literal, walk);
	CHECK_EQ(walks, 259);
	return check::finish();
}
