// AS3's arithmetic and conversions on machine numbers, each as the interpreter's loop does it
// (vm/interpreter_run.cpp): what C++ alone leaves undefined goes through vm/numeric.h and vm/runtime.h.
#pragma once

#include "numeric.h"
#include "values.h"

#include <cmath>

namespace kas3 {
namespace rt {

// ECMAScript's ToInt32 and ToUint32. A Number that is already in range needs only its fraction dropped;
// the rest (a wrap, an infinity, not-a-number) is the shared routine's.
inline int32_t to_i32(Context *c, double p_value) {
	if (p_value >= -2147483648.0 && p_value < 2147483648.0) {
		return static_cast<int32_t>(p_value);
	}
	return to_int32(c, p_value);
}
inline uint32_t to_u32(Context *c, double p_value) {
	if (p_value >= 0.0 && p_value < 4294967296.0) {
		return static_cast<uint32_t>(p_value);
	}
	return to_uint32(c, p_value);
}

// An int or a uint as the int the bitwise operators work on: the same bits.
inline int32_t i32(int32_t p_value) {
	return p_value;
}
inline int32_t i32(uint32_t p_value) {
	return static_cast<int32_t>(p_value);
}

// Comparisons as functions: a value compared with itself (`x == x`, how a program asks for not-a-number)
// is then nothing a C++ compiler warns of.
template <typename T>
inline bool less(T p_a, T p_b) {
	return p_a < p_b;
}
template <typename T>
inline bool less_or_equal(T p_a, T p_b) {
	return p_a <= p_b;
}
template <typename T>
inline bool equal(T p_a, T p_b) {
	return p_a == p_b;
}

inline bool to_bool(double p_value) {
	return !(p_value == 0 || std::isnan(p_value));
}

inline int32_t shl(int32_t p_value, int32_t p_count) {
	return static_cast<int32_t>(static_cast<uint32_t>(p_value) << (static_cast<uint32_t>(p_count) & 31));
}
inline int32_t shr(int32_t p_value, int32_t p_count) {
	return p_value >> (static_cast<uint32_t>(p_count) & 31);
}
inline uint32_t ushr(int32_t p_value, int32_t p_count) {
	return static_cast<uint32_t>(p_value) >> (static_cast<uint32_t>(p_count) & 31);
}

// Int64: arithmetic wraps at 64 bits.
inline int64_t add_wide(int64_t p_a, int64_t p_b) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_a) + static_cast<uint64_t>(p_b));
}
inline int64_t sub_wide(int64_t p_a, int64_t p_b) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_a) - static_cast<uint64_t>(p_b));
}
inline int64_t mul_wide(int64_t p_a, int64_t p_b) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_a) * static_cast<uint64_t>(p_b));
}
inline int64_t neg_wide(int64_t p_a) {
	return static_cast<int64_t>(0 - static_cast<uint64_t>(p_a));
}
inline int64_t shl_wide(int64_t p_value, int64_t p_count) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_value) << (static_cast<uint64_t>(p_count) & 63));
}
inline int64_t ushr_wide(int64_t p_value, int64_t p_count) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_value) >> (static_cast<uint64_t>(p_count) & 63));
}

} // namespace rt
} // namespace kas3
