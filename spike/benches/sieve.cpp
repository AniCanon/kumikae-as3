// Hand translation of as3/Sieve.as.
#include "benches/shape.h"

namespace {

const int32_t LIMIT = 200000;

struct Sieve : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<as3::Vector<int32_t>> flags{ as3::Vector<int32_t>::create(LIMIT) };
	int32_t total = 0;
};
AS3_CLASS(Sieve)

void Sieve__process(Sieve *self, double delta) {
	AS3_FRAME("Sieve/_process", "Sieve.as");
	// This function makes no calls, so the field cannot change under it: it is read and checked once.
	// Every index is still bounds-checked, and every statement that can throw stores its line.
	AS3_LINE(13);
	as3::Vector<int32_t> *flags = self->flags.get();
	AS3_NULLCHECK_VOID(flags);
	int32_t i;
	for (i = 0; i < LIMIT; i = as3::add_int(i, 1)) {
		// Sieve.as:13   flags[i] = 1;
		AS3_LINE(13);
		AS3_BOUNDS_VOID(flags, i);
		flags->data[i] = 1;
	}
	int32_t count = 0;
	for (i = 2; i < LIMIT; i = as3::add_int(i, 1)) {
		// Sieve.as:17   if (flags[i] == 1) {
		AS3_LINE(17);
		AS3_BOUNDS_VOID(flags, i);
		if (flags->data[i] == 1) {
			// Sieve.as:18   count++;
			count = as3::add_int(count, 1);
			// Sieve.as:19   var j:int = i + i;
			int32_t j = as3::add_int(i, i);
			while (j < LIMIT) {
				// Sieve.as:21   flags[j] = 0;
				AS3_LINE(21);
				AS3_BOUNDS_VOID(flags, j);
				flags->data[j] = 0;
				// Sieve.as:22   j += i;
				j = as3::add_int(j, i);
			}
		}
	}
	// Sieve.as:25   total += count;
	self->total = as3::add_int(self->total, count);
}

double Sieve_checksum(Sieve *self) {
	// Sieve.as:29   return total;
	return (double)self->total;
}

AS3_THUNK_DELTA(Sieve, _process)
AS3_THUNK_RET_NUMBER(Sieve, checksum)

const as3::MethodEntry methods[] = {
	{ "_process", Sieve_thunk__process },
	{ "checksum", Sieve_thunk_checksum },
};
const as3::ScriptClass script_class = { "Sieve", "Node2D", Sieve_create, methods, 2, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
