// Godot's value types that have parts, as generated code holds them: plain structs laid out as Godot lays
// them out (a build with single-precision reals), held inline in a local and passed to Godot by address.
// No Godot header: the sizes are the API description's, and a test in Godot compares what the maths here
// gives with what Godot gives.
//
// The maths is Godot's formulas (core/math), written out so that a C++ compiler can inline them. Only what
// is here is done inline; any other operation on one of these values is Godot's own, asked for through
// the host.
#pragma once

#include <cmath>
#include <cstdint>

namespace kas3 {
namespace rt {

struct Vector2 {
	float x, y;
};
struct Vector2i {
	int32_t x, y;
};
struct Rect2 {
	Vector2 position, size;
};
struct Rect2i {
	Vector2i position, size;
};
struct Vector3 {
	float x, y, z;
};
struct Vector3i {
	int32_t x, y, z;
};
struct Transform2D {
	Vector2 columns[3];
};
struct Vector4 {
	float x, y, z, w;
};
struct Vector4i {
	int32_t x, y, z, w;
};
struct Plane {
	Vector3 normal;
	float d;
};
struct Quaternion {
	float x, y, z, w;
};
struct AABB {
	Vector3 position, size;
};
struct Basis {
	Vector3 rows[3];
};
struct Transform3D {
	Basis basis;
	Vector3 origin;
};
struct Projection {
	Vector4 columns[4];
};
struct Color {
	float r, g, b, a;
};

static_assert(sizeof(Vector2) == 8 && sizeof(Vector2i) == 8 && sizeof(Rect2) == 16 && sizeof(Rect2i) == 16, "as Godot's (float_32)");
static_assert(sizeof(Vector3) == 12 && sizeof(Vector3i) == 12 && sizeof(Transform2D) == 24 && sizeof(Vector4) == 16, "as Godot's (float_32)");
static_assert(sizeof(Vector4i) == 16 && sizeof(Plane) == 16 && sizeof(Quaternion) == 16 && sizeof(AABB) == 24, "as Godot's (float_32)");
static_assert(sizeof(Basis) == 36 && sizeof(Transform3D) == 48 && sizeof(Projection) == 64 && sizeof(Color) == 16, "as Godot's (float_32)");

// Which of them a value is, where the host must be told (boxing one into its handle): the numbers are this
// runtime's own, and the host learns Godot's for each by the name (PARTS_NAMES, in this order).
enum class Parts : uint32_t {
	Vector2, Vector2i, Rect2, Rect2i, Vector3, Vector3i, Transform2D, Vector4, Vector4i, Plane, Quaternion, AABB, Basis, Transform3D, Projection, Color, Count
};
inline constexpr const char *PARTS_NAMES[] = { "Vector2", "Vector2i", "Rect2", "Rect2i", "Vector3", "Vector3i", "Transform2D", "Vector4", "Vector4i",
	"Plane", "Quaternion", "AABB", "Basis", "Transform3D", "Projection", "Color" };

// Vector2 (core/math/vector2.h). A Number given to Godot where a real is taken is rounded to one first.
inline Vector2 vector2(double p_x, double p_y) {
	return Vector2{ static_cast<float>(p_x), static_cast<float>(p_y) };
}
inline Vector2 add(Vector2 p_a, Vector2 p_b) {
	return Vector2{ p_a.x + p_b.x, p_a.y + p_b.y };
}
inline Vector2 subtract(Vector2 p_a, Vector2 p_b) {
	return Vector2{ p_a.x - p_b.x, p_a.y - p_b.y };
}
inline Vector2 multiply(Vector2 p_a, double p_by) {
	const float by = static_cast<float>(p_by);
	return Vector2{ p_a.x * by, p_a.y * by };
}
inline Vector2 divide(Vector2 p_a, double p_by) {
	const float by = static_cast<float>(p_by);
	return Vector2{ p_a.x / by, p_a.y / by };
}
inline double length(Vector2 p_a) {
	return std::sqrt(p_a.x * p_a.x + p_a.y * p_a.y);
}
inline Vector2 normalized(Vector2 p_a) {
	float l = p_a.x * p_a.x + p_a.y * p_a.y;
	if (l != 0) {
		l = std::sqrt(l);
		p_a.x /= l;
		p_a.y /= l;
	}
	return p_a;
}

} // namespace rt
} // namespace kas3
