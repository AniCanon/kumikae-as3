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

inline double dot(Vector2 p_a, Vector2 p_b) {
	return p_a.x * p_b.x + p_a.y * p_b.y;
}

// Transform2D (core/math/transform_2d.cpp). Godot rounds rotation and skew before adding them.
inline Transform2D transform2d(double p_rotation, Vector2 p_position) {
	const float angle = static_cast<float>(p_rotation);
	const float cosine = std::cos(angle), sine = std::sin(angle);
	return Transform2D{ { { cosine, sine }, { -sine, cosine }, p_position } };
}

inline Transform2D transform2d(double p_rotation, Vector2 p_scale, double p_skew, Vector2 p_position) {
	const float angle = static_cast<float>(p_rotation);
	const float skewed = angle + static_cast<float>(p_skew);
	return Transform2D{ { { std::cos(angle) * p_scale.x, std::sin(angle) * p_scale.x },
			{ -std::sin(skewed) * p_scale.y, std::cos(skewed) * p_scale.y }, p_position } };
}

// Vector3 (core/math/vector3.h).
inline Vector3 add(Vector3 p_a, Vector3 p_b) {
	return Vector3{ p_a.x + p_b.x, p_a.y + p_b.y, p_a.z + p_b.z };
}
inline Vector3 subtract(Vector3 p_a, Vector3 p_b) {
	return Vector3{ p_a.x - p_b.x, p_a.y - p_b.y, p_a.z - p_b.z };
}
inline Vector3 multiply(Vector3 p_a, double p_by) {
	const float by = static_cast<float>(p_by);
	return Vector3{ p_a.x * by, p_a.y * by, p_a.z * by };
}
inline Vector3 divide(Vector3 p_a, double p_by) {
	const float by = static_cast<float>(p_by);
	return Vector3{ p_a.x / by, p_a.y / by, p_a.z / by };
}
inline double dot(Vector3 p_a, Vector3 p_b) {
	return p_a.x * p_b.x + p_a.y * p_b.y + p_a.z * p_b.z;
}
inline Vector3 cross(Vector3 p_a, Vector3 p_b) {
	return Vector3{ (p_a.y * p_b.z) - (p_a.z * p_b.y), (p_a.z * p_b.x) - (p_a.x * p_b.z), (p_a.x * p_b.y) - (p_a.y * p_b.x) };
}
inline double length(Vector3 p_a) {
	const float x2 = p_a.x * p_a.x;
	const float y2 = p_a.y * p_a.y;
	const float z2 = p_a.z * p_a.z;
	return std::sqrt(x2 + y2 + z2);
}
inline Vector3 normalized(Vector3 p_a) {
	const float squared = p_a.x * p_a.x + p_a.y * p_a.y + p_a.z * p_a.z;
	if (squared == 0) {
		return Vector3{ 0, 0, 0 };
	}
	const float l = std::sqrt(squared);
	return Vector3{ p_a.x / l, p_a.y / l, p_a.z / l };
}

} // namespace rt
} // namespace kas3
