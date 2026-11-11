// Boilerplate for version 2 translations: pooled allocation and the same engine-call thunks as version 1.
#pragma once

#include "benches/shape.h"
#include "runtime/as3rt2.h"

// Class record for a class allocated from the size-class pool (rule 14).
#define AS3_CLASS2(Name) \
	void Name##_destroy(as3::Object *p_object) { \
		as3::pool_delete(static_cast<Name *>(p_object)); \
	} \
	const as3::Class Name##_class = { #Name, Name##_destroy }; \
	as3::Object *Name##_create(GDExtensionObjectPtr p_owner) { \
		Name *self = as3::pool_new<Name>(); \
		self->refcount = 0; \
		self->cls = &Name##_class; \
		self->owner = p_owner; \
		return self; \
	}
