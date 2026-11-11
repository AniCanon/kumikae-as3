// Hand translation of as3/Sieve.as under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t LIMIT = 200000;

struct V2Sieve : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<as3::Vector<int32_t>> flags{ as3::Vector<int32_t>::create(LIMIT) };
	int32_t total = 0;
};
AS3_CLASS2(V2Sieve)

// The fully checked version of the loops, taken when the guard below fails. It throws at exactly the
// iteration the unversioned code would have.
__attribute__((noinline)) void V2Sieve__process_checked(V2Sieve *self, int32_t *const data, const int32_t length) {
	AS3_FN("Sieve/_process", "Sieve.as");
	int32_t i;
	for (i = 0; i < LIMIT; i = as3::add_int(i, 1)) {
		AS3_CHECK_BOUNDS_VOID(i, length, 13);
		data[i] = 1;
	}
	int32_t count = 0;
	for (i = 2; i < LIMIT; i = as3::add_int(i, 1)) {
		AS3_CHECK_BOUNDS_VOID(i, length, 17);
		if (data[i] == 1) {
			count = as3::add_int(count, 1);
			int32_t j = as3::add_int(i, i);
			while (j < LIMIT) {
				AS3_CHECK_BOUNDS_VOID(j, length, 21);
				data[j] = 0;
				j = as3::add_int(j, i);
			}
		}
	}
	self->total = as3::add_int(self->total, count);
}

void V2Sieve__process(V2Sieve *self, double delta) {
	AS3_FN("Sieve/_process", "Sieve.as");
	// This function makes no calls, so the vector's length and data live in locals for all of it (rule 7),
	// and `total` is read once and written back at the exit (rule 12).
	as3::Vector<int32_t> *flags = self->flags.get();
	AS3_CHECK_NULL_VOID(flags, 13);
	const int32_t length = flags->length;
	int32_t *const data = flags->data;
	// Loop versioning (rule 7). Every index below is in [0, LIMIT): `i` counts up to LIMIT, and `j` starts
	// at i + i with a positive stride i, stops at LIMIT, and cannot wrap because LIMIT + i < 2^31. So one
	// test proves every access in range.
	if (AS3_UNLIKELY(LIMIT > length)) {
		V2Sieve__process_checked(self, data, length);
		return;
	}
	int32_t i;
	for (i = 0; i < LIMIT; i = as3::add_int(i, 1)) {
		// Sieve.as:13   flags[i] = 1;   (bounds check elided: guard above, rule 7)
		data[i] = 1;
	}
	int32_t count = 0;
	for (i = 2; i < LIMIT; i = as3::add_int(i, 1)) {
		// Sieve.as:17   if (flags[i] == 1) {
		if (data[i] == 1) {
			count = as3::add_int(count, 1);
			// Sieve.as:19   var j:int = i + i;
			int32_t j = as3::add_int(i, i);
			while (j < LIMIT) {
				// Sieve.as:21   flags[j] = 0;
				data[j] = 0;
				j = as3::add_int(j, i);
			}
		}
	}
	// Sieve.as:25   total += count;
	self->total = as3::add_int(self->total, count);
}

double V2Sieve_checksum(V2Sieve *self) {
	return (double)self->total;
}

AS3_THUNK_DELTA(V2Sieve, _process)
AS3_THUNK_RET_NUMBER(V2Sieve, checksum)

const as3::MethodEntry methods[] = {
	{ "_process", V2Sieve_thunk__process },
	{ "checksum", V2Sieve_thunk_checksum },
};
const as3::ScriptClass script_class = { "V2Sieve", "Node2D", V2Sieve_create, methods, 2, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
