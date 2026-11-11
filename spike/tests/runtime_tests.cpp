// Tests for the AS3-shaped runtime core. No Godot, no framework.
#include "runtime/as3_collections.h"
#include "runtime/as3rt.h"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

static int destroyed = 0;
static void destroy_thing(as3::Object *p_object) {
	destroyed++;
	delete p_object;
}
static const as3::Class thing_class = { "Thing", destroy_thing };

static void ref_counts() {
	destroyed = 0;
	as3::Object *raw = new as3::Object{ 0, &thing_class };
	{
		as3::Ref<as3::Object> a(raw);
		as3::Ref<as3::Object> b = a;
		CHECK(raw->refcount == 2);
		CHECK(destroyed == 0);
	}
	CHECK(destroyed == 1);
}

static int deref(as3::Object *p_object) {
	AS3_FRAME("deref", "t.as");
	AS3_LINE(1);
	AS3_NULLCHECK(p_object);
	return 42;
}

static void null_check_sets_error() {
	as3::clear_error();
	CHECK(deref(nullptr) == 0);
	CHECK(as3::pending_error != nullptr);
	CHECK(as3::pending_error && std::strcmp(as3::pending_error->type, "TypeError") == 0);
	CHECK(as3::pending_error && std::strcmp(as3::pending_error->message,
			"Error #1009: Cannot access a property or method of a null object reference.") == 0);
	as3::clear_error();
	as3::Object thing{ 1, &thing_class };
	CHECK(deref(&thing) == 42);
	CHECK(as3::pending_error == nullptr);
}

static void c() {
	AS3_FRAME("c", "t.as");
	AS3_LINE(7);
	as3::throw_error("Error", "boom");
}
static void b() {
	AS3_FRAME("b", "t.as");
	AS3_LINE(4);
	c();
	if (AS3_PENDING()) {
		return;
	}
	AS3_LINE(5);
}
static void a() {
	AS3_FRAME("a", "t.as");
	AS3_LINE(2);
	b();
	if (AS3_PENDING()) {
		return;
	}
	AS3_LINE(3);
}

static void error_propagates_through_frames() {
	as3::clear_error();
	a();
	CHECK(as3::pending_error != nullptr);
	CHECK(std::strcmp(as3::pending_trace(), "\tat c() [t.as:7]\n\tat b() [t.as:4]\n\tat a() [t.as:2]\n") == 0);
	CHECK(as3::top_frame == nullptr);
	as3::clear_error();
	CHECK(as3::pending_error == nullptr);
}

static void vector_bounds() {
	as3::clear_error();
	as3::Ref<as3::Vector<int32_t>> v(as3::Vector<int32_t>::create());
	v->push(10);
	v->push(20);
	v->push(30);
	CHECK(v->length == 3);
	CHECK(v->at(2) == 30);
	CHECK(as3::pending_error == nullptr);
	CHECK(v->at(5) == 0);
	CHECK(as3::pending_error && std::strcmp(as3::pending_error->type, "RangeError") == 0);
	as3::clear_error();
	CHECK(v->at(-1) == 0);
	CHECK(as3::pending_error != nullptr);
	as3::clear_error();
}

static void array_holds_tagged_values() {
	destroyed = 0;
	as3::Object *raw = new as3::Object{ 0, &thing_class };
	{
		as3::Ref<as3::Array> arr(as3::Array::create());
		arr->push(as3::Value(7));
		arr->push(as3::Value(1.5));
		arr->push(as3::Value(raw));
		CHECK(arr->length() == 3);
		CHECK(arr->at(0).tag == as3::Value::INT && arr->at(0).i == 7);
		CHECK(arr->at(1).tag == as3::Value::NUMBER && arr->at(1).n == 1.5);
		CHECK(raw->refcount == 1);
		CHECK(arr->at(99).tag == as3::Value::UNDEFINED);
	}
	CHECK(destroyed == 1);
}

static void dict_order_and_missing() {
	as3::Ref<as3::Dict<std::string>> d(as3::Dict<std::string>::create());
	d->set("b", as3::Value(1));
	d->set("a", as3::Value(2));
	d->set("b", as3::Value(3));
	CHECK(d->size() == 2);
	CHECK(d->key_at(0) == "b");
	CHECK(d->key_at(1) == "a");
	CHECK(d->get("b").i == 3);
	CHECK(d->get("zzz").tag == as3::Value::UNDEFINED);
}

static void int_wraps() {
	CHECK(as3::add_int(2147483647, 1) == -2147483647 - 1);
	CHECK(as3::mul_int(65536, 65536) == 0);
}

int main() {
	ref_counts();
	null_check_sets_error();
	error_propagates_through_frames();
	vector_bounds();
	array_holds_tagged_values();
	dict_order_and_missing();
	int_wraps();
	if (failures == 0) {
		std::printf("runtime tests: OK\n");
	}
	return failures == 0 ? 0 : 1;
}
