// Minimal assertions for the compiler's unit tests. No framework.
#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

namespace check {

inline int &checks() {
	static int value = 0;
	return value;
}
inline int &failures() {
	static int value = 0;
	return value;
}

template <class T>
std::string show(const T &p_value) {
	std::ostringstream out;
	out << p_value;
	return out.str();
}

inline int finish() {
	std::printf("%d checks, %d failed\n", checks(), failures());
	return failures() == 0 ? 0 : 1;
}

} // namespace check

#define CHECK(cond) \
	do { \
		check::checks()++; \
		if (!(cond)) { \
			check::failures()++; \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_EQ(actual, expected) \
	do { \
		check::checks()++; \
		const auto &check_a_ = (actual); \
		const auto &check_b_ = (expected); \
		if (!(check_a_ == check_b_)) { \
			check::failures()++; \
			std::printf("FAIL %s:%d: %s\n  actual:   %s\n  expected: %s\n", __FILE__, __LINE__, #actual, \
					check::show(check_a_).c_str(), check::show(check_b_).c_str()); \
		} \
	} while (0)
