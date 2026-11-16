// The library's functions of plain values (vm/library.h), run with no interpreter: a heap over a module
// made by hand, and a runner that records what it is asked.
#include "check.h"

#include "any.h"
#include "library.h"
#include "no_engine.h"

#include <string>
#include <vector>

using namespace kas3::vm;
using kas3::ir::Kind;
using kas3::ir::StringFunction;

namespace {

// Answers as whoever runs the program would, and keeps what it was asked.
struct Recorder final : Runner {
	std::vector<std::string> raised;
	uint32_t raised_class = kas3::ir::NONE;
	int memory = 0;
	int calls = 0;
	std::string says = "?";
	void raise(uint32_t p_class, const std::string &p_message) override {
		raised_class = p_class;
		raised.push_back(p_message);
	}
	void out_of_memory() override { memory++; }
	bool call(Object *, const Slot *p_arguments, size_t p_count, Slot *r_result) override {
		calls++;
		*r_result = p_count ? p_arguments[0] : any::undefined();
		return true;
	}
	bool text(Slot p_value, std::string *r_text) override {
		*r_text = any::text(p_value).value_or(says);
		return true;
	}
};

constexpr uint32_t ARRAY = 0, ERROR = 1, URI_ERROR = 2;

// Array, Error and URIError, as a program's module has them.
kas3::ir::Module module_of_three() {
	kas3::ir::Module module;
	kas3::ir::Class array;
	array.name = "Array";
	array.element = Kind::Any;
	array.array = true;
	module.classes.push_back(array);
	for (const char *name : { "Error", "URIError" }) {
		kas3::ir::Class error;
		error.name = name;
		for (const char *field : { "message", "name", "errorID", "stack" }) {
			kas3::ir::Field made;
			made.name = field;
			made.kind = std::string(field) == "errorID" ? Kind::I32 : Kind::Str;
			error.fields.push_back(made);
		}
		module.classes.push_back(error);
	}
	module.array_class = ARRAY;
	module.error_class = ERROR;
	module.uri_error_class = URI_ERROR;
	return module;
}

Slot of_text(Str *p_text) {
	Slot slot;
	slot.bits = 0;
	slot.p = p_text;
	return slot;
}

Slot of_number(double p_value) {
	Slot slot;
	slot.bits = 0;
	slot.d = p_value;
	return slot;
}

std::string element_text(const Vector *p_vector, uint32_t p_index) {
	return std::string(static_cast<const Str *>(any::pointer(p_vector->data[p_index]))->view());
}

void test_split() {
	const kas3::ir::Module module = module_of_three();
	Heap heap(module, NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder };
	Str *text = new_string("a,b,,c");
	Str *comma = new_string(",");
	Slot out = of_number(0);
	const Slot by_comma[] = { of_text(text), any::string(comma), of_number(4294967295.0) };
	CHECK(text_call(library, StringFunction::Split, by_comma, 3, &out));
	Vector *pieces = static_cast<Vector *>(out.p);
	CHECK_EQ(pieces->length, 4u);
	CHECK_EQ(element_text(pieces, 1), std::string("b"));
	CHECK_EQ(element_text(pieces, 2), std::string(""));
	heap.release_object(pieces);
	// No delimiter is the whole text; a limit cuts the pieces short.
	const Slot whole[] = { of_text(text), any::undefined(), of_number(4294967295.0) };
	CHECK(text_call(library, StringFunction::Split, whole, 3, &out));
	CHECK_EQ(static_cast<Vector *>(out.p)->length, 1u);
	heap.release_object(static_cast<Object *>(out.p));
	const Slot two[] = { of_text(text), any::string(comma), of_number(2) };
	CHECK(text_call(library, StringFunction::Split, two, 3, &out));
	CHECK_EQ(static_cast<Vector *>(out.p)->length, 2u);
	heap.release_object(static_cast<Object *>(out.p));
	// A delimiter that is no String is put into words by whoever runs the program.
	recorder.says = "b";
	const Slot by_number[] = { of_text(text), any::boolean(true), of_number(4294967295.0) };
	CHECK(text_call(library, StringFunction::Split, by_number, 3, &out));
	CHECK_EQ(static_cast<Vector *>(out.p)->length, 1u); // "true" is nowhere in it.
	heap.release_object(static_cast<Object *>(out.p));
	for (Str *each : { text, comma }) {
		if (release(each)) {
			free_string(each);
		}
	}
	CHECK(recorder.raised.empty());
	CHECK_EQ(live_objects(), static_cast<size_t>(0));
}

void test_uri() {
	const kas3::ir::Module module = module_of_three();
	Heap heap(module, NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder };
	Str *spaced = new_string("a b");
	Str *broken = new_string("%");
	Slot out = of_number(0);
	const Slot plain[] = { of_text(spaced) };
	CHECK(text_call(library, StringFunction::EncodeURI, plain, 1, &out));
	Str *made = static_cast<Str *>(out.p);
	CHECK_EQ(std::string(made->view()), std::string("a%20b"));
	const Slot bad[] = { of_text(broken) };
	out = of_number(7);
	CHECK(!text_call(library, StringFunction::DecodeURI, bad, 1, &out));
	CHECK_EQ(out.d, 7.0); // Nothing is written when it raises.
	CHECK_EQ(recorder.raised.size(), static_cast<size_t>(1));
	CHECK_EQ(recorder.raised_class, URI_ERROR);
	CHECK(recorder.raised[0].find("Error #1052") == 0);
	for (Str *each : { spaced, broken, made }) {
		if (release(each)) {
			free_string(each);
		}
	}
}

void test_new_error() {
	const kas3::ir::Module module = module_of_three();
	Heap heap(module, NoEngine::instance());
	Object *error = new_error(heap, URI_ERROR, new_string("Error #1052: Invalid URI passed to decodeURI function."));
	CHECK(error != nullptr);
	CHECK_EQ(error->fields()[kas3::ir::ERROR_ID].i, 1052);
	CHECK_EQ(std::string(static_cast<Str *>(error->fields()[kas3::ir::ERROR_NAME].p)->view()), std::string("URIError"));
	CHECK(error->fields()[kas3::ir::ERROR_STACK].p == nullptr);
	heap.release_object(error);
	CHECK(new_error(heap, kas3::ir::NONE, nullptr) == nullptr);
	Str *lost = new_string("lost");
	fail_allocations(1);
	CHECK(new_error(heap, ERROR, lost) == nullptr); // With no memory: the message is let go of.
	fail_allocations(0);
	CHECK_EQ(live_objects(), static_cast<size_t>(0));
	CHECK_EQ(live_strings(), static_cast<size_t>(0));
}

} // namespace

int main() {
	test_split();
	test_uri();
	test_new_error();
	return check::finish();
}
