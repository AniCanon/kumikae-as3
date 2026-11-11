// Hand translation of as3/Saxpy.as (holdout) under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t N = 100000;
const int32_t PASSES = 10;
const double A = 0.000001;

struct V2Saxpy : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<as3::Vector<double>> x{ as3::Vector<double>::create(N) };
	as3::Ref<as3::Vector<double>> y{ as3::Vector<double>::create(N) };
	double last = 0.0;
};
AS3_CLASS2(V2Saxpy)

void V2Saxpy__ready(V2Saxpy *self) {
	AS3_FN("Saxpy/_ready", "Saxpy.as");
	as3::Vector<double> *x = self->x.get();
	as3::Vector<double> *y = self->y.get();
	AS3_CHECK_NULL_VOID(x, 16);
	AS3_CHECK_NULL_VOID(y, 17);
	for (int32_t i = 0; i < N; i = as3::add_int(i, 1)) {
		AS3_CHECK_BOUNDS_VOID(i, x->length, 16);
		x->data[i] = (double)i * 0.5;
		AS3_CHECK_BOUNDS_VOID(i, y->length, 17);
		y->data[i] = (double)i;
	}
}

__attribute__((noinline)) void V2Saxpy__process_checked(V2Saxpy *self, double *const xd, const int32_t xn,
		double *const yd, const int32_t yn) {
	AS3_FN("Saxpy/_process", "Saxpy.as");
	int32_t i;
	for (int32_t p = 0; p < PASSES; p = as3::add_int(p, 1)) {
		for (i = 0; i < N; i = as3::add_int(i, 1)) {
			AS3_CHECK_BOUNDS_VOID(i, xn, 25);
			AS3_CHECK_BOUNDS_VOID(i, yn, 25);
			yd[i] = A * xd[i] + yd[i];
		}
	}
	double sum = 0.0;
	for (i = 0; i < N; i = as3::add_int(i, 1)) {
		AS3_CHECK_BOUNDS_VOID(i, yn, 30);
		sum += yd[i];
	}
	self->last = sum;
}

void V2Saxpy__process(V2Saxpy *self, double delta) {
	AS3_FN("Saxpy/_process", "Saxpy.as");
	// No calls in this function: both vectors' lengths and data live in locals (rule 7).
	as3::Vector<double> *x = self->x.get();
	as3::Vector<double> *y = self->y.get();
	AS3_CHECK_NULL_VOID(x, 25);
	AS3_CHECK_NULL_VOID(y, 25);
	const int32_t xn = x->length;
	const int32_t yn = y->length;
	double *const xd = x->data;
	double *const yd = y->data;
	// Loop versioning (rule 7): every index is in [0, N), so one test covers every access.
	if (AS3_UNLIKELY(N > xn || N > yn)) {
		V2Saxpy__process_checked(self, xd, xn, yd, yn);
		return;
	}
	int32_t i;
	for (int32_t p = 0; p < PASSES; p = as3::add_int(p, 1)) {
		for (i = 0; i < N; i = as3::add_int(i, 1)) {
			// Saxpy.as:25   y[i] = A * x[i] + y[i];   (bounds checks elided: guard above, rule 7)
			yd[i] = A * xd[i] + yd[i];
		}
	}
	double sum = 0.0;
	for (i = 0; i < N; i = as3::add_int(i, 1)) {
		// Saxpy.as:30   sum += y[i];
		sum += yd[i];
	}
	// Saxpy.as:32   last = sum;
	self->last = sum;
}

double V2Saxpy_checksum(V2Saxpy *self) {
	return self->last;
}

AS3_THUNK_VOID(V2Saxpy, _ready)
AS3_THUNK_DELTA(V2Saxpy, _process)
AS3_THUNK_RET_NUMBER(V2Saxpy, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", V2Saxpy_thunk__ready },
	{ "_process", V2Saxpy_thunk__process },
	{ "checksum", V2Saxpy_thunk_checksum },
};
const as3::ScriptClass script_class = { "V2Saxpy", "Node2D", V2Saxpy_create, methods, 3, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
