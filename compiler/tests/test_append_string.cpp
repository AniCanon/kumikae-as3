#include "check.h"
#include "runtime.h"

using namespace kas3::vm;

static void test_shared_or_self(bool p_self) {
	Str *prefix = new_string("old");
	Str *piece = p_self ? prefix : new_string("!");
	if (!p_self) {
		retain(prefix);
	}
	Str *joined = append_string(&prefix, piece);
	CHECK(prefix != nullptr);
	if (prefix) {
		CHECK(prefix->view() == "old");
		CHECK(joined != prefix);
		CHECK_EQ(prefix->references, p_self ? 1u : 2u);
		if (!p_self) {
			CHECK(!release(prefix));
		}
		free_string(prefix);
	}
	CHECK(joined && joined->view() == (p_self ? "oldold" : "old!"));
	free_string(joined);
	if (!p_self) {
		free_string(piece);
	}
}

static void test_length(bool p_left, bool p_right) {
	Str *prefix = new_string("a😀");
	Str *piece = new_string("é");
	if (p_left) {
		CHECK_EQ(prefix->length(), 3);
	}
	if (p_right) {
		CHECK_EQ(piece->length(), 1);
	}
	Str *joined = append_string(&prefix, piece);
	CHECK(!prefix);
	CHECK(joined && joined->measured() == (p_left || p_right));
	CHECK(joined && joined->length() == 4 && joined->view() == "a😀é");
	free_string(joined);
	free_string(piece);
}

static void test_boundary(size_t p_size) {
	const std::string text(p_size - 1, 'x');
	Str *prefix = new_string(text), *piece = new_string("y");
	Str *joined = append_string(&prefix, piece);
	const bool large = p_size >= UINT16_MAX;
	CHECK((prefix != nullptr) == large);
	if (prefix) {
		CHECK(prefix->view() == text);
	}
	// A long result must have the extended header before its bytes can be inspected.
	if (joined && (!large || prefix)) {
		CHECK_EQ(joined->is_long(), large);
		CHECK(joined->view() == text + "y");
		CHECK_EQ(joined->data()[p_size], '\0');
	} else {
		CHECK(joined != nullptr);
	}
	if (prefix) {
		free_string(prefix);
	}
	free_string(joined);
	free_string(piece);
}

static void test_failed_growth() {
	Str *prefix = new_string("old"), *piece = new_string("!");
	Str *before = prefix;
	fail_allocations(1);
	Str *joined = append_string(&prefix, piece);
	fail_allocations(0);
	CHECK(!joined && prefix == before);
	CHECK(prefix && prefix->view() == "old" && prefix->references == 1);
	if (joined) {
		free_string(joined);
	} else {
		free_string(before);
	}
	free_string(piece);
}

int main() {
	test_shared_or_self(false);
	test_shared_or_self(true);
	for (bool left : { false, true }) {
		for (bool right : { false, true }) {
			test_length(left, right);
		}
	}
	for (size_t size : { 65534, 65535, 65536 }) {
		test_boundary(size);
	}
	test_failed_growth();
	CHECK_EQ(live_strings(), size_t(0));
	return check::finish();
}
