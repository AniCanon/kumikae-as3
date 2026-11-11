// Boilerplate shared by the hand translations: the class record and the engine-call thunks a compiler would emit.
#pragma once

#include "benches/binds.h"
#include "host/class_registry.h"
#include "runtime/godot_calls.h"

#include <cmath>

// Destructor and constructor glue for a class whose C++ struct default-initialises its fields.
#define AS3_CLASS(Name) \
	void Name##_destroy(as3::Object *p_object) { \
		delete static_cast<Name *>(p_object); \
	} \
	const as3::Class Name##_class = { #Name, Name##_destroy }; \
	as3::Object *Name##_create(GDExtensionObjectPtr p_owner) { \
		Name *self = new Name(); \
		self->refcount = 0; \
		self->cls = &Name##_class; \
		self->owner = p_owner; \
		return self; \
	}

// Thunks: unbox the engine's Variant arguments and call the typed method (rule 10).
#define AS3_THUNK_VOID(Name, method) \
	void Name##_thunk_##method(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, \
			GDExtensionVariantPtr) { \
		Name##_##method(static_cast<Name *>(p_self)); \
	}
#define AS3_THUNK_DELTA(Name, method) \
	void Name##_thunk_##method(as3::Object *p_self, const GDExtensionConstVariantPtr *p_args, int64_t, \
			GDExtensionVariantPtr) { \
		Name##_##method(static_cast<Name *>(p_self), as3::arg_number(p_args[0])); \
	}
#define AS3_THUNK_RET_NUMBER(Name, method) \
	void Name##_thunk_##method(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, \
			GDExtensionVariantPtr r_ret) { \
		as3::ret_number(r_ret, Name##_##method(static_cast<Name *>(p_self))); \
	}
