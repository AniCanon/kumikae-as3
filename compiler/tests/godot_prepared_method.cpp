// What a prepared method of a native program accepts, decided before Godot is asked anything: a call
// that does not fit runs through its thunk instead.
#include "check.h"
#include "native_method.h"

int main() {
	using godot::Variant;
	using kas3::keeps_default;
	using kas3::prepared_arity;
	// The program's signature and the running Godot's must have as many parameters: the generated code's
	// argument array has a place for each one the program was built against.
	CHECK(prepared_arity("RID,Rect2,RID,Rect2,Color,bool,bool", 7));
	CHECK(!prepared_arity("RID,Rect2,RID,Rect2,Color,bool,bool", 8));
	CHECK(!prepared_arity("RID,Rect2,RID,Rect2,Color,bool", 7));
	CHECK(prepared_arity("-", 0));
	CHECK(!prepared_arity("-", 1));
	// A default kept once for every call must not be shared state a call could change.
	for (const int kept : { Variant::BOOL, Variant::INT, Variant::FLOAT, Variant::COLOR, Variant::VECTOR2, Variant::VECTOR2I, Variant::RECT2,
				 Variant::TRANSFORM2D, Variant::RID, Variant::PACKED_INT32_ARRAY, Variant::PACKED_STRING_ARRAY }) {
		CHECK(keeps_default(kept));
	}
	for (const int refused : { Variant::NIL, Variant::OBJECT, Variant::STRING, Variant::STRING_NAME, Variant::NODE_PATH, Variant::ARRAY,
				 Variant::DICTIONARY, Variant::CALLABLE, Variant::SIGNAL }) {
		CHECK(!keeps_default(refused));
	}
	return check::finish();
}
