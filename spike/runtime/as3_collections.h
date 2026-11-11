// AS3's own collection types: Vector.<T>, Array, and Object/Dictionary.
#pragma once

#include "runtime/as3rt.h"

#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace as3 {

// Vector.<T>: contiguous, typed, bounds-checked on every index.
template <class T>
struct Vector : Object {
	T *data = nullptr;
	int32_t length = 0;
	int32_t capacity = 0;

	static void destroy(Object *p_object) {
		Vector *self = static_cast<Vector *>(p_object);
		for (int32_t i = 0; i < self->length; i++) {
			self->data[i].~T();
		}
		std::free(self->data);
		delete self;
	}
	static const Class *class_info() {
		static const Class info = { "Vector", destroy };
		return &info;
	}
	static Vector *create() {
		Vector *v = new Vector();
		v->refcount = 0;
		v->cls = class_info();
		return v;
	}
	static Vector *create(int32_t p_length) {
		Vector *v = create();
		v->reserve(p_length);
		for (int32_t i = 0; i < p_length; i++) {
			new (&v->data[i]) T();
		}
		v->length = p_length;
		return v;
	}

	void reserve(int32_t p_capacity) {
		if (p_capacity <= capacity) {
			return;
		}
		T *grown = static_cast<T *>(std::malloc(sizeof(T) * (size_t)p_capacity));
		for (int32_t i = 0; i < length; i++) {
			new (&grown[i]) T(std::move(data[i]));
			data[i].~T();
		}
		std::free(data);
		data = grown;
		capacity = p_capacity;
	}

	T at(int32_t p_index) const {
		if (AS3_UNLIKELY((uint32_t)p_index >= (uint32_t)length)) {
			throw_range();
			return T();
		}
		return data[p_index];
	}

	void set(int32_t p_index, const T &p_value) {
		if (AS3_UNLIKELY((uint32_t)p_index >= (uint32_t)length)) {
			throw_range();
			return;
		}
		data[p_index] = p_value;
	}

	void push(const T &p_value) {
		if (length == capacity) {
			reserve(capacity ? capacity * 2 : 8);
		}
		new (&data[length++]) T(p_value);
	}
};

// An untyped AS3 value.
struct Value {
	enum Tag : uint8_t { UNDEFINED, NULL_VALUE, INT, NUMBER, BOOLEAN, OBJECT };
	Tag tag = UNDEFINED;
	union {
		int32_t i;
		double n;
		bool b;
		Object *o;
	};

	Value() :
			n(0) {}
	explicit Value(int32_t p_i) :
			tag(INT), i(p_i) {}
	explicit Value(double p_n) :
			tag(NUMBER), n(p_n) {}
	explicit Value(bool p_b) :
			tag(BOOLEAN), b(p_b) {}
	explicit Value(Object *p_o) :
			tag(p_o ? OBJECT : NULL_VALUE), o(p_o) {
		retain(p_o);
	}
	Value(const Value &p_other) {
		std::memcpy(static_cast<void *>(this), &p_other, sizeof(Value));
		if (tag == OBJECT) {
			retain(o);
		}
	}
	Value &operator=(const Value &p_other) {
		if (p_other.tag == OBJECT) {
			retain(p_other.o);
		}
		if (tag == OBJECT) {
			release(o);
		}
		std::memcpy(static_cast<void *>(this), &p_other, sizeof(Value));
		return *this;
	}
	~Value() {
		if (tag == OBJECT) {
			release(o);
		}
	}
};

// Array: dynamic, untyped. Reading past the end yields undefined, as in AS3.
struct Array : Object {
	std::vector<Value> items;

	static void destroy(Object *p_object) { delete static_cast<Array *>(p_object); }
	static const Class *class_info() {
		static const Class info = { "Array", destroy };
		return &info;
	}
	static Array *create() {
		Array *a = new Array();
		a->refcount = 0;
		a->cls = class_info();
		return a;
	}
	int32_t length() const { return (int32_t)items.size(); }
	void push(const Value &p_value) { items.push_back(p_value); }
	Value at(int32_t p_index) const {
		if ((uint32_t)p_index >= (uint32_t)items.size()) {
			return Value();
		}
		return items[(size_t)p_index];
	}
};

// Object literals and Dictionary: insertion-ordered map from a string key to an untyped value.
template <class K, class H = std::hash<K>>
struct Dict : Object {
	std::vector<std::pair<K, Value>> entries;
	std::unordered_map<K, size_t, H> index;

	static void destroy(Object *p_object) { delete static_cast<Dict *>(p_object); }
	static const Class *class_info() {
		static const Class info = { "Object", destroy };
		return &info;
	}
	static Dict *create() {
		Dict *d = new Dict();
		d->refcount = 0;
		d->cls = class_info();
		return d;
	}
	int32_t size() const { return (int32_t)entries.size(); }
	const K &key_at(int32_t p_index) const { return entries[(size_t)p_index].first; }
	void set(const K &p_key, const Value &p_value) {
		auto found = index.find(p_key);
		if (found != index.end()) {
			entries[found->second].second = p_value;
			return;
		}
		index.emplace(p_key, entries.size());
		entries.emplace_back(p_key, p_value);
	}
	Value get(const K &p_key) const {
		auto found = index.find(p_key);
		if (found == index.end()) {
			return Value();
		}
		return entries[found->second].second;
	}
};

} // namespace as3
