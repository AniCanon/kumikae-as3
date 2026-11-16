// The library's functions of plain values (vm/library.h), run with no interpreter: a heap over a module
// made by hand, and a runner that records what it is asked.
#include "check.h"

#include "any.h"
#include "library.h"
#include "no_engine.h"

#include <cmath>
#include <string>
#include <vector>

using namespace kas3::vm;
using kas3::ir::Kind;
using kas3::ir::StringFunction;

namespace {

// Answers as whoever runs the program would, and keeps what it was asked.
struct Recorder final : Runner, Unmoved {
	bool failing() const override { return !raised.empty() || memory > 0; }
	// Nothing here is still the interpreter's: these answer plainly.
	bool object_of_engine(Slot, uint32_t, bool *, Slot *) override { return false; }
	bool engine_is(Slot, uint32_t) override { return false; }
	bool array_number_get(Vector *, double, Slot *) override { return true; }
	bool array_number_set(Vector *, double, Slot) override { return true; }
	bool sort_field(Slot, Str *, Slot *) override { return true; }

	std::vector<std::string> raised;
	uint32_t raised_class = kas3::ir::NONE;
	int memory = 0;
	// Each function of the program it was asked to run, with how many values; what such a run gives.
	std::vector<std::pair<uint32_t, size_t>> ran;
	Slot gives{};
	void raise(uint32_t p_class, const std::string &p_message) override {
		raised_class = p_class;
		raised.push_back(p_message);
	}
	void out_of_memory() override { memory++; }
	bool run(uint32_t p_function, const Slot *, size_t p_count, Slot *r_result) override {
		ran.emplace_back(p_function, p_count);
		*r_result = gives;
		retain(static_cast<Str *>(gives.p)); // Every function of test_words() gives a String.
		return true;
	}
	uint32_t carries() const override { return 0; }
	uint32_t runner_depth() const { return wording; }
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
	Heap heap(module, NoEngine::instance(), NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder, recorder };
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
	// A delimiter that is no String is put into words.
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
	Heap heap(module, NoEngine::instance(), NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder, recorder };
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
	Heap heap(module, NoEngine::instance(), NoEngine::instance());
	Object *error = new_error(heap, URI_ERROR, new_string("Error #1052: Invalid URI passed to decodeURI function."));
	CHECK(error != nullptr);
	CHECK_EQ(own_field(error, kas3::ir::ERROR_ID).i, 1052);
	CHECK_EQ(std::string(static_cast<Str *>(own_field(error, kas3::ir::ERROR_NAME).p)->view()), std::string("URIError"));
	CHECK(own_field(error, kas3::ir::ERROR_STACK).p == nullptr);
	heap.release_object(error);
	CHECK(new_error(heap, kas3::ir::NONE, nullptr) == nullptr);
	Str *lost = new_string("lost");
	fail_allocations(1);
	CHECK(new_error(heap, ERROR, lost) == nullptr); // With no memory: the message is let go of.
	fail_allocations(0);
	CHECK_EQ(live_objects(), static_cast<size_t>(0));
	CHECK_EQ(live_strings(), static_cast<size_t>(0));
}

void test_number_text() {
	kas3::ir::Module module = module_of_three();
	module.range_error_class = ERROR;
	Heap heap(module, NoEngine::instance(), NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder, recorder };
	Slot out = of_number(0);
	const Slot hex[] = { of_number(255), of_number(16) };
	CHECK(number_text(library, kas3::ir::NumberFormat::Radix, false, hex, 2, &out));
	Str *made = static_cast<Str *>(out.p);
	CHECK_EQ(std::string(made->view()), std::string("ff"));
	if (release(made)) {
		free_string(made);
	}
	Slot wide = of_number(0);
	wide.l = -9007199254740993;
	const Slot fixed[] = { wide, of_number(2) };
	CHECK(number_text(library, kas3::ir::NumberFormat::Fixed, true, fixed, 2, &out));
	made = static_cast<Str *>(out.p);
	CHECK_EQ(std::string(made->view()), std::string("-9007199254740993.00"));
	if (release(made)) {
		free_string(made);
	}
	// A radix out of range is the RangeError the program's runner is asked to raise.
	const Slot radix[] = { of_number(255), of_number(37) };
	out = of_number(7);
	CHECK(!number_text(library, kas3::ir::NumberFormat::Radix, false, radix, 2, &out));
	CHECK_EQ(out.d, 7.0);
	CHECK_EQ(recorder.raised.size(), static_cast<size_t>(1));
	CHECK(recorder.raised[0].find("Error #1003") == 0);
	CHECK(number_words(library, kas3::ir::NumberFormat::Precision, of_number(1.5), false, 0) == std::nullopt);
	CHECK_EQ(number_words(library, kas3::ir::NumberFormat::Precision, of_number(NAN), false, 0).value_or("?"), std::string("NaN"));
	CHECK_EQ(live_strings(), static_cast<size_t>(0));
}

Slot of_int(int32_t p_value) {
	Slot slot;
	slot.bits = 0;
	slot.i = p_value;
	return slot;
}

void test_vectors() {
	using kas3::ir::VectorMethod;
	kas3::ir::Module module = module_of_three();
	module.range_error_class = ERROR;
	kas3::ir::Class ints;
	ints.name = "Vector.<int>";
	ints.element = Kind::I32;
	module.classes.push_back(ints);
	const uint32_t INTS = 3;
	Heap heap(module, NoEngine::instance(), NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder, recorder };
	Vector *vector = vector_new(library, INTS, 0, false);
	CHECK(vector != nullptr);
	Slot out = of_number(0);
	const Slot push[] = { of_text(nullptr), of_int(7), of_int(9), of_int(11) };
	Slot pushed[4] = { push[0], push[1], push[2], push[3] };
	pushed[0].p = vector;
	CHECK(vector_call(library, VectorMethod::Push, pushed, 4, &out));
	CHECK_EQ(out.u, 3u);
	CHECK(vector_call(library, VectorMethod::Push, pushed, 2, nullptr)); // Nobody wants the new length.
	CHECK_EQ(vector->length, 4u);
	const Slot find[] = { pushed[0], of_int(9), of_int(0) };
	CHECK(vector_call(library, VectorMethod::IndexOf, find, 3, &out));
	CHECK_EQ(out.i, 1);
	CHECK(vector_call(library, VectorMethod::Pop, pushed, 1, &out));
	CHECK_EQ(out.i, 7);
	const Slot part[] = { pushed[0], of_int(1), of_int(16777215) };
	CHECK(vector_call(library, VectorMethod::Slice, part, 3, &out));
	Vector *sliced = static_cast<Vector *>(out.p);
	CHECK_EQ(sliced->length, 2u);
	CHECK_EQ(sliced->data[1].i, 11);
	heap.release_object(sliced);
	// An element by an index of any kind; one past the end is added, further out is the RangeError.
	CHECK(vector_get(library, vector, Kind::F64, of_number(2), &out));
	CHECK_EQ(out.i, 11);
	CHECK(vector_set(library, vector, Kind::I32, of_int(3), of_int(13)));
	CHECK_EQ(vector->length, 4u);
	CHECK(!vector_set(library, vector, Kind::I32, of_int(9), of_int(1)));
	CHECK(recorder.raised.back().find("Error #1125") == 0);
	recorder.raised.clear();
	// A fixed vector's length does not change.
	vector->fixed = true;
	CHECK(!vector_call(library, VectorMethod::Push, pushed, 2, &out));
	CHECK(recorder.raised.back().find("Error #1126") == 0);
	CHECK(!vector_set_length(library, vector, 4));
	recorder.raised.clear();
	// No vector at all is the TypeError.
	module.type_error_class = ERROR;
	const Slot none[] = { of_text(nullptr) };
	CHECK(!vector_call(library, VectorMethod::Pop, none, 1, &out));
	CHECK(recorder.raised.back().find("Error #1009") == 0);
	heap.release_object(vector);
	// new Array(3) is three empty places; new Array("3") holds the one String.
	const Slot three[] = { any::integer(3) };
	Vector *array = array_new(library, ARRAY, three, 1);
	CHECK_EQ(array->length, 3u);
	heap.release_object(array);
	const Slot bad[] = { any::number(1.5) };
	CHECK(array_new(library, ARRAY, bad, 1) == nullptr);
	CHECK(recorder.raised.back().find("Error #1005") == 0);
	CHECK_EQ(live_objects(), static_cast<size_t>(0));
}

void test_untyped() {
	kas3::ir::Module module = module_of_three();
	module.type_error_class = ERROR;
	module.range_error_class = ERROR;
	Heap heap(module, NoEngine::instance(), NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder, recorder };
	// A whole Number is held as an int where it fits one, and as an Int64 up to 2^53.
	CHECK(any::tag(to_any(library, Kind::F64, of_number(3))) == AnyTag::Int);
	CHECK(any::tag(to_any(library, Kind::F64, of_number(3.5))) == AnyTag::Number);
	CHECK(any::tag(to_any(library, Kind::F64, of_number(-0.0))) == AnyTag::Number);
	const Slot large = to_any(library, Kind::F64, of_number(4294967296.0));
	CHECK(any::tag(large) == AnyTag::Wide);
	Slot out = of_number(0);
	CHECK(from_any(library, large, Kind::I64, kas3::ir::NONE, &out));
	CHECK(out.l == 4294967296);
	CHECK(from_any(library, large, Kind::I32, kas3::ir::NONE, &out));
	CHECK_EQ(out.i, 0); // An Int64 wraps into an int by its own low bits.
	CHECK(any_is(library, large, kas3::ir::ANY_IS_NUMBER) && !any_is(library, large, kas3::ir::ANY_IS_INT));
	heap.drop_any(large);
	// The operators: whole numbers stay whole, text joins, and text is ordered as text.
	const Slot numbers[] = { any::integer(3), any::integer(4) };
	CHECK(any_operate(library, kas3::ir::Op::AnyAdd, numbers, 2, &out));
	CHECK(any::tag(out) == AnyTag::Int && any::to_number(out) == 7);
	CHECK(any_operate(library, kas3::ir::Op::AnyLt, numbers, 2, &out) && out.b);
	CHECK(any_operate(library, kas3::ir::Op::AnyGe, numbers, 2, &out) && !out.b);
	CHECK(any_operate(library, kas3::ir::Op::AnyStrictEq, numbers, 2, &out) && !out.b);
	// `-` and the rest make both Numbers, and give an untyped value; unary `-` takes one.
	CHECK(any_operate(library, kas3::ir::Op::AnySub, numbers, 2, &out) && any::to_number(out) == -1);
	CHECK(any_operate(library, kas3::ir::Op::AnyMul, numbers, 2, &out) && any::to_number(out) == 12);
	CHECK(any_operate(library, kas3::ir::Op::AnyDiv, numbers, 2, &out) && any::to_number(out) == 0.75);
	CHECK(any_operate(library, kas3::ir::Op::AnyMod, numbers, 2, &out) && any::to_number(out) == 3);
	CHECK(any_operate(library, kas3::ir::Op::AnyNeg, numbers, 1, &out) && any::to_number(out) == -3);
	CHECK(any_operator_gives(kas3::ir::Op::AnyNeg) == Kind::Any && any_operator_gives(kas3::ir::Op::AnyLe) == Kind::Bool);
	Str *ten = new_string("10");
	const Slot mixed[] = { any::string(ten), any::integer(9) };
	CHECK(any_operate(library, kas3::ir::Op::AnyAdd, mixed, 2, &out));
	CHECK_EQ(std::string(static_cast<Str *>(any::pointer(out))->view()), std::string("109"));
	heap.drop_any(out);
	CHECK(any_operate(library, kas3::ir::Op::AnyGt, mixed, 2, &out) && out.b); // 10 > 9, as numbers.
	CHECK(any_operate(library, kas3::ir::Op::AnySub, mixed, 2, &out) && any::to_number(out) == 1); // And so is "10" - 9.
	const Slot same[] = { any::string(ten), any::integer(10) };
	CHECK(any_operate(library, kas3::ir::Op::AnyEq, same, 2, &out) && out.b);
	CHECK(any_operate(library, kas3::ir::Op::AnyStrictEq, same, 2, &out) && !out.b);
	CHECK(element_is(library, Kind::Any, same[0], same[0]) && !element_is(library, Kind::Any, same[0], same[1]));
	if (release(ten)) {
		free_string(ten);
	}
	// Into a String: null and undefined are null, a number is its text.
	CHECK(from_any(library, any::undefined(), Kind::Str, kas3::ir::NONE, &out));
	CHECK(out.p == nullptr);
	CHECK(from_any(library, any::integer(12), Kind::Str, kas3::ir::NONE, &out));
	Str *twelve = static_cast<Str *>(out.p);
	CHECK_EQ(std::string(twelve->view()), std::string("12"));
	if (release(twelve)) {
		free_string(twelve);
	}
	// An object where one of another class is wanted is the TypeError; of its own class or none, itself.
	Object *error = new_error(heap, ERROR, nullptr);
	CHECK(from_any(library, any::object(error), Kind::Obj, ERROR, &out));
	CHECK(out.p == error);
	heap.release_object(error);
	CHECK(is_class(module, error, ERROR) && !is_class(module, error, URI_ERROR) && !is_class(module, nullptr, ERROR));
	CHECK(any_is(library, any::object(error), ERROR) && !any_is(library, any::null(), kas3::ir::ANY_IS_OBJECT));
	CHECK(!from_any(library, any::object(error), Kind::Obj, ARRAY, &out));
	CHECK(recorder.raised.back().find("Error #1034: Type Coercion failed: cannot convert") == 0);
	recorder.raised.clear();
	CHECK(!from_any(library, any::number(1.5), Kind::I64, kas3::ir::NONE, &out));
	CHECK_EQ(recorder.raised.size(), static_cast<size_t>(1));
	heap.release_object(error);
	CHECK_EQ(live_objects(), static_cast<size_t>(0));
}

void test_making() {
	kas3::ir::Module module = module_of_three();
	kas3::ir::Class point;
	point.name = "Point";
	kas3::ir::Field x;
	x.name = "x";
	x.kind = Kind::F64;
	point.fields.push_back(x);
	kas3::ir::Field next;
	next.name = "next";
	next.kind = Kind::Obj;
	point.fields.push_back(next);
	module.classes.push_back(point);
	kas3::ir::Class table;
	table.name = "Object";
	table.table = true;
	module.classes.push_back(table);
	Heap heap(module, NoEngine::instance(), NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder, recorder };
	// A Number field is not yet a number; an object that can hold another is on the collector's list.
	Object *made = object_new(library, 3);
	CHECK(made != nullptr && std::isnan(field(module.classes[3], made, 0).d) && field(module.classes[3], made, 1).p == nullptr);
	CHECK(made->gc_next != nullptr);
	heap.release_object(made);
	Object *object = table_new(library, 4, false);
	CHECK(object != nullptr);
	heap.release_object(object);
	CHECK(table_new(library, 4, true) == nullptr);
	CHECK_EQ(recorder.raised.size(), static_cast<size_t>(1));
	fail_allocations(1);
	CHECK(object_new(library, 3) == nullptr);
	CHECK_EQ(recorder.memory, 1);
	CHECK_EQ(live_objects(), static_cast<size_t>(0));
}

// A value's words and a function value's call run the program by a function's number, with typed values.
void test_words() {
	constexpr uint32_t FUNCTION = 3, POINT = 4, SAY = 0, VALUE = 1, JOIN = 2;
	kas3::ir::Module module = module_of_three();
	module.type_error_class = ERROR;
	module.argument_error_class = ERROR;
	kas3::ir::Class function;
	function.name = "Function";
	function.fields.resize(2);
	function.fields[kas3::ir::FUNCTION_INDEX].kind = Kind::I32;
	function.fields[kas3::ir::FUNCTION_FIRST].kind = Kind::Obj;
	module.classes.push_back(function);
	module.function_class = FUNCTION;
	kas3::ir::Class point;
	point.name = "geom.Point";
	point.to_string = SAY;
	point.methods = { SAY, VALUE };
	module.classes.push_back(point);
	for (const char *name : { "geom.Point/toString", "geom.Point/valueOf", "join" }) {
		kas3::ir::Function made;
		made.name = name;
		made.result = Kind::Str;
		made.owner = POINT;
		made.parameters = made.required = 1;
		made.registers = { Kind::Obj };
		module.functions.push_back(made);
	}
	kas3::ir::Function &join = module.functions[JOIN];
	join.owner = kas3::ir::NONE;
	join.is_static = true;
	join.parameters = 2;
	join.registers = { Kind::F64, Kind::Str };
	Heap heap(module, NoEngine::instance(), NoEngine::instance());
	Recorder recorder;
	Library library{ heap, recorder, recorder };
	Str *said = new_string("(1, 2)");
	recorder.gives = of_text(said);
	Object *made = object_new(library, POINT);
	// String(x) of an object is its class's toString(), run on the object alone.
	std::string words;
	CHECK(any_text(library, any::object(made), &words));
	CHECK_EQ(words, std::string("(1, 2)"));
	CHECK(recorder.ran.size() == 1 && recorder.ran[0] == std::make_pair(SAY, static_cast<size_t>(1)));
	// An Array's words are its elements', and nothing of it and no undefined is written.
	Vector *list = vector_new(library, ARRAY, 3, false);
	list->data[0] = any::integer(7);
	list->data[2] = any::object(made);
	retain(made);
	words.clear();
	CHECK(vector_text(library, list, "-", &words));
	CHECK_EQ(words, std::string("7--(1, 2)"));
	CHECK_EQ(recorder.runner_depth(), 0u);
	heap.release_object(list);
	// As a primitive it is what its valueOf() gives.
	recorder.ran.clear();
	Slot primitive = any::undefined();
	CHECK(any_primitive(library, any::object(made), &primitive));
	CHECK(any::tag(primitive) == AnyTag::String && recorder.ran.size() == 1 && recorder.ran[0].first == VALUE);
	heap.drop_any(primitive);
	CHECK_EQ(object_text(library, made), std::string("[object Point]"));
	// A function value is called with each argument as its parameter holds it; too many is the ArgumentError.
	Object *value = object_new(library, FUNCTION);
	own_field(value, kas3::ir::FUNCTION_INDEX).i = static_cast<int32_t>(JOIN);
	recorder.ran.clear();
	const Slot given[] = { any::integer(4), any::integer(5), any::integer(6) };
	Slot result = any::undefined();
	CHECK(function_call(library, value, given, 2, &result));
	CHECK(any::tag(result) == AnyTag::String && recorder.ran.size() == 1 && recorder.ran[0] == std::make_pair(JOIN, static_cast<size_t>(2)));
	heap.drop_any(result);
	CHECK(!function_call(library, value, given, 3, &result));
	// The message says how many are required, whichever way the count is wrong (avmplus's rule).
	CHECK(recorder.raised.back().find("Error #1063: Argument count mismatch on join(). Expected 1, got 3.") == 0);
	// A function that names `arguments` takes more than it declares: the others go with the call, untyped.
	module.functions[JOIN].takes_extra = true;
	recorder.ran.clear();
	CHECK(function_call(library, value, given, 3, &result));
	CHECK(recorder.ran.size() == 1 && recorder.ran[0] == std::make_pair(JOIN, static_cast<size_t>(3)));
	heap.drop_any(result);
	module.functions[JOIN].takes_extra = false;
	CHECK(!function_call(library, made, given, 0, &result));
	CHECK(recorder.raised.back().find("Error #1006") == 0);
	// A static function has one value, the same each time; a method's is made for its object each time.
	Object *lone = make_function(library, JOIN, nullptr);
	CHECK(lone != nullptr && make_function(library, JOIN, nullptr) == lone);
	heap.release_object(lone);
	heap.release_object(lone);
	CHECK_EQ(function_length(module, lone), 2);
	Object *bound = make_method(library, 1, made);
	CHECK(bound != nullptr);
	CHECK(own_field(bound, kas3::ir::FUNCTION_FIRST).p == made && static_cast<uint32_t>(own_field(bound, kas3::ir::FUNCTION_INDEX).i) == VALUE);
	CHECK(make_method(library, 1, nullptr) == nullptr);
	heap.release_object(bound);
	heap.release_lone_functions();
	heap.release_object(value);
	heap.release_object(made);
	if (release(said)) {
		free_string(said);
	}
	CHECK_EQ(live_objects(), static_cast<size_t>(0));
	CHECK_EQ(live_strings(), static_cast<size_t>(0));
}

} // namespace

int main() {
	test_words();
	test_making();
	test_untyped();
	test_vectors();
	test_number_text();
	test_split();
	test_uri();
	test_new_error();
	return check::finish();
}
