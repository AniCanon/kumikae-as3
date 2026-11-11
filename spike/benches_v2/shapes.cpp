// Hand translation of as3/Shapes.as (holdout) under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t COUNT = 9000;
const int32_t PASSES = 10;
const double PI = 3.141592653589793;

// Shape and its method table (rule 15). Overridable methods are called through the class record.
struct Shape : as3::Object {};
struct ShapeClass {
	as3::Class base;
	double (*area)(Shape *);
	void (*grow)(Shape *, double);
};

struct Circle : Shape {
	double r;
};
struct Rect : Shape {
	double w, h;
};
struct Tri : Shape {
	double b, h;
};

double Circle_area(Shape *p_self) {
	Circle *self = static_cast<Circle *>(p_self);
	return PI * self->r * self->r;
}
void Circle_grow(Shape *p_self, double d) {
	static_cast<Circle *>(p_self)->r += d;
}
double Rect_area(Shape *p_self) {
	Rect *self = static_cast<Rect *>(p_self);
	return self->w * self->h;
}
void Rect_grow(Shape *p_self, double d) {
	Rect *self = static_cast<Rect *>(p_self);
	self->w += d;
	self->h += d;
}
double Tri_area(Shape *p_self) {
	Tri *self = static_cast<Tri *>(p_self);
	return 0.5 * self->b * self->h;
}
void Tri_grow(Shape *p_self, double d) {
	static_cast<Tri *>(p_self)->b += d;
}

void Circle_destroy(as3::Object *p_object) {
	as3::pool_delete(static_cast<Circle *>(p_object));
}
void Rect_destroy(as3::Object *p_object) {
	as3::pool_delete(static_cast<Rect *>(p_object));
}
void Tri_destroy(as3::Object *p_object) {
	as3::pool_delete(static_cast<Tri *>(p_object));
}
const ShapeClass Circle_class = { { "Circle", Circle_destroy }, Circle_area, Circle_grow };
const ShapeClass Rect_class = { { "Rect", Rect_destroy }, Rect_area, Rect_grow };
const ShapeClass Tri_class = { { "Tri", Tri_destroy }, Tri_area, Tri_grow };

Shape *Circle_new(double r) {
	Circle *self = as3::pool_new<Circle>();
	self->refcount = 0;
	self->cls = &Circle_class.base;
	self->r = r;
	return self;
}
Shape *Rect_new(double w, double h) {
	Rect *self = as3::pool_new<Rect>();
	self->refcount = 0;
	self->cls = &Rect_class.base;
	self->w = w;
	self->h = h;
	return self;
}
Shape *Tri_new(double b, double h) {
	Tri *self = as3::pool_new<Tri>();
	self->refcount = 0;
	self->cls = &Tri_class.base;
	self->b = b;
	self->h = h;
	return self;
}

typedef as3::Vector<as3::Ref<Shape>> ShapeVector;

struct V2Shapes : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<ShapeVector> shapes{ ShapeVector::create() };
	double total = 0.0;
};
AS3_CLASS2(V2Shapes)

void V2Shapes__ready(V2Shapes *self) {
	AS3_FN("Shapes/_ready", "Shapes.as");
	ShapeVector *shapes = self->shapes.get();
	AS3_CHECK_NULL_VOID(shapes, 16);
	for (int32_t i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		double size = 1.0 + (double)i * 0.001;
		switch (i % 3) {
			case 0:
				shapes->push(as3::Ref<Shape>(Circle_new(size)));
				break;
			case 1:
				shapes->push(as3::Ref<Shape>(Rect_new(size, 2.0)));
				break;
			default:
				shapes->push(as3::Ref<Shape>(Tri_new(3.0, size)));
		}
	}
}

void V2Shapes__process(V2Shapes *self, double delta) {
	AS3_FN("Shapes/_process", "Shapes.as");
	// Shapes.as:24   var n:int = shapes.length;
	// The only calls below are grow() and area(). Whole-program analysis shows no override of either
	// writes `shapes`, resizes any vector, or throws. So the vector's length and data stay in locals
	// (rule 7), the element local borrows (rule 2), and the calls need no error test (rule 15).
	ShapeVector *shapes = self->shapes.get();
	AS3_CHECK_NULL_VOID(shapes, 24);
	const int32_t n = shapes->length;
	as3::Ref<Shape> *const data = shapes->data;
	// Shapes.as:25   var sum:Number = total;
	double sum = self->total;
	for (int32_t p = 0; p < PASSES; p = as3::add_int(p, 1)) {
		for (int32_t i = 0; i < n; i = as3::add_int(i, 1)) {
			// Shapes.as:28   var s:Shape = shapes[i];   (bounds check elided: i < n, rule 7)
			Shape *s = data[i].get();
			// Shapes.as:29   s.grow(0.001);
			AS3_CHECK_NULL_VOID(s, 29);
			const ShapeClass *cls = reinterpret_cast<const ShapeClass *>(s->cls);
			cls->grow(s, 0.001);
			// Shapes.as:30   sum += s.area();   (null check elided: checked at line 29, rule 3)
			sum += cls->area(s);
		}
	}
	// Shapes.as:33   total = sum;
	self->total = sum;
}

double V2Shapes_checksum(V2Shapes *self) {
	return self->total;
}

AS3_THUNK_VOID(V2Shapes, _ready)
AS3_THUNK_DELTA(V2Shapes, _process)
AS3_THUNK_RET_NUMBER(V2Shapes, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", V2Shapes_thunk__ready },
	{ "_process", V2Shapes_thunk__process },
	{ "checksum", V2Shapes_thunk_checksum },
};
const as3::ScriptClass script_class = { "V2Shapes", "Node2D", V2Shapes_create, methods, 3, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
