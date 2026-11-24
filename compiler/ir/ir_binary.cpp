#include "ir_binary.h"
#include "last_use.h"
#include "ir_binary_check.h"

#include <cstring>
#include <utility>

namespace kas3 {
namespace ir {

namespace {

constexpr uint8_t MAGIC[4] = { 'K', 'A', 'S', '3' };
constexpr uint8_t KIND_MAX = static_cast<uint8_t>(Kind::U8);
constexpr uint8_t BARE_MAX = static_cast<uint8_t>(Bare::Vector2i);
constexpr uint8_t FORM_MAX = static_cast<uint8_t>(Constant::Form::Value);
constexpr uint8_t OP_MAX = OP_COUNT - 1;

class Writer {
	std::vector<uint8_t> out_;

public:
	std::vector<uint8_t> take() { return std::move(out_); }

	void byte(uint8_t p_value) { out_.push_back(p_value); }

	void flag(bool p_value) { byte(p_value ? 1u : 0u); }

	void u32(uint32_t p_value) {
		while (p_value >= 0x80u) {
			out_.push_back(static_cast<uint8_t>((p_value & 0x7Fu) | 0x80u));
			p_value >>= 7;
		}
		out_.push_back(static_cast<uint8_t>(p_value));
	}

	void i64(int64_t p_value) {
		uint64_t encoded = (static_cast<uint64_t>(p_value) << 1) ^ static_cast<uint64_t>(p_value >> 63);
		while (encoded >= 0x80u) {
			out_.push_back(static_cast<uint8_t>((encoded & 0x7Fu) | 0x80u));
			encoded >>= 7;
		}
		out_.push_back(static_cast<uint8_t>(encoded));
	}

	void f64(double p_value) {
		uint64_t bits = 0;
		std::memcpy(&bits, &p_value, sizeof(bits));
		for (int i = 0; i < 8; i++) {
			out_.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFFu));
		}
	}

	void text(std::string_view p_text) {
		u32(static_cast<uint32_t>(p_text.size()));
		out_.insert(out_.end(), p_text.begin(), p_text.end());
	}

	void u32_list(const std::vector<uint32_t> &p_items) {
		u32(static_cast<uint32_t>(p_items.size()));
		for (uint32_t item : p_items) {
			u32(item);
		}
	}

	void text_list(const std::vector<std::string> &p_items) {
		u32(static_cast<uint32_t>(p_items.size()));
		for (const std::string &item : p_items) {
			text(item);
		}
	}

	void constant(const Constant &p_value) {
		byte(static_cast<uint8_t>(p_value.form));
		if (p_value.form == Constant::Form::Number || p_value.form == Constant::Form::Boolean) {
			f64(p_value.number);
		} else if (p_value.form == Constant::Form::Text) {
			text(p_value.text);
		} else if (p_value.form == Constant::Form::Value) {
			text(p_value.text);
			u32(static_cast<uint32_t>(p_value.parts.size()));
			for (double part : p_value.parts) {
				f64(part);
			}
		}
	}

	void field(const Field &p_field) {
		text(p_field.name);
		byte(static_cast<uint8_t>(p_field.kind));
		u32(p_field.klass);
		const uint8_t bits = (p_field.starts_null ? 1u : 0u) | (p_field.is_public ? 2u : 0u) |
				(p_field.is_protected ? 4u : 0u) | (p_field.constant ? 8u : 0u) | (p_field.exported ? 16u : 0u);
		byte(bits);
		text(p_field.engine_type);
		constant(p_field.first);
	}

	void klass(const Class &p_class) {
		text(p_class.name);
		u32(p_class.base);
		byte(static_cast<uint8_t>(p_class.element));
		u32(p_class.element_check);
		text(p_class.element_type);
		byte(static_cast<uint8_t>(p_class.element_bare));
		const uint16_t bits = (p_class.element_zero ? 1u : 0u) | (p_class.element_counted ? 2u : 0u) |
				(p_class.array ? 4u : 0u) | (p_class.table ? 8u : 0u) | (p_class.dictionary ? 16u : 0u) |
				(p_class.built_in ? 32u : 0u) | (p_class.counted ? 64u : 0u) | (p_class.is_interface ? 128u : 0u);
		u32(bits);
		u32(static_cast<uint32_t>(p_class.fields.size()));
		for (const Field &each : p_class.fields) {
			field(each);
		}
		u32_list(p_class.methods);
		u32(static_cast<uint32_t>(p_class.named_methods.size()));
		for (const auto &[name, slot] : p_class.named_methods) {
			text(name);
			u32(slot);
		}
		u32(p_class.constructor);
		u32(p_class.static_init);
		u32(p_class.to_string);
		text(p_class.native_base);
		u32(static_cast<uint32_t>(p_class.virtuals.size()));
		for (const auto &[name, fn] : p_class.virtuals) {
			text(name);
			u32(fn);
		}
		u32_list(p_class.interfaces);
		u32_list(p_class.interface_methods);
		text_list(p_class.signals);
	}

	void function(const Function &p_fn) {
		text(p_fn.name);
		u32(p_fn.file);
		u32(p_fn.position);
		u32(p_fn.owner);
		u32(p_fn.written_in);
		const uint8_t bits = (p_fn.is_static ? 1u : 0u) | (p_fn.is_public ? 2u : 0u) |
				(p_fn.rest ? 4u : 0u) | (p_fn.takes_extra ? 8u : 0u) | (p_fn.bound_engine_method ? 16u : 0u);
		byte(bits);
		u32(p_fn.parameters);
		u32(p_fn.required);
		byte(static_cast<uint8_t>(p_fn.result));
		text_list(p_fn.engine_types);
		u32_list(p_fn.parameter_classes);
		u32(static_cast<uint32_t>(p_fn.virtual_receivers.size()));
		for (const auto &[at, klass_id] : p_fn.virtual_receivers) {
			u32(at);
			u32(klass_id);
		}
		u32(static_cast<uint32_t>(p_fn.registers.size()));
		for (Kind kind : p_fn.registers) {
			byte(static_cast<uint8_t>(kind));
		}
		u32(static_cast<uint32_t>(p_fn.code.size()));
		for (const Instruction &in : p_fn.code) {
			byte(static_cast<uint8_t>(in.op.execution));
			u32(in.a);
			u32(in.b);
			u32(in.c);
			u32(in.position);
		}
		u32(static_cast<uint32_t>(p_fn.argument_lists.size()));
		for (const auto &list : p_fn.argument_lists) {
			u32_list(list);
		}
		u32(static_cast<uint32_t>(p_fn.locals.size()));
		for (const Local &local : p_fn.locals) {
			text(local.name);
			u32(local.reg);
		}
		u32(static_cast<uint32_t>(p_fn.handlers.size()));
		for (const Handler &h : p_fn.handlers) {
			u32(h.start);
			u32(h.end);
			u32(h.target);
			u32(h.reg);
			u32(h.type);
			flag(h.rethrows);
		}
	}
};

class Reader {
	const uint8_t *data_;
	size_t size_;
	size_t at_ = 0;
	std::string error_;

	bool fail(std::string p_message) {
		if (error_.empty()) {
			error_ = std::move(p_message);
		}
		return false;
	}


public:
	Reader(const uint8_t *p_data, size_t p_size) : data_(p_data), size_(p_size) {}

	const std::string &error() const { return error_; }
	bool at_end() const { return at_ == size_; }

	bool check_count(uint32_t p_count, size_t p_min_bytes = 1) {
		return p_count <= (size_ - at_) / p_min_bytes || fail("truncated binary IR module");
	}

	bool byte(uint8_t *r_out) {
		if (at_ >= size_) {
			return fail("truncated binary IR module");
		}
		*r_out = data_[at_++];
		return true;
	}

	bool flag(bool *r_out) {
		uint8_t raw = 0;
		if (!byte(&raw)) {
			return false;
		}
		if (raw > 1u) {
			return fail("invalid Boolean in binary IR module");
		}
		*r_out = raw != 0;
		return true;
	}

	bool u32(uint32_t *r_out) {
		uint32_t value = 0;
		for (unsigned shift = 0; shift < 35; shift += 7) {
			uint8_t b = 0;
			if (!byte(&b)) {
				return false;
			}
			if (shift == 28 && (b & 0xF0u) != 0) {
				return fail("integer overflow in binary IR module");
			}
			value |= static_cast<uint32_t>(b & 0x7Fu) << shift;
			if ((b & 0x80u) == 0) {
				*r_out = value;
				return true;
			}
		}
		return fail("integer overflow in binary IR module");
	}

	bool i64(int64_t *r_out) {
		uint64_t encoded = 0;
		for (unsigned shift = 0; shift < 70; shift += 7) {
			uint8_t b = 0;
			if (!byte(&b)) {
				return false;
			}
			if (shift == 63 && b > 1u) {
				return fail("wide integer overflow in binary IR module");
			}
			encoded |= static_cast<uint64_t>(b & 0x7Fu) << shift;
			if ((b & 0x80u) == 0) {
				*r_out = static_cast<int64_t>((encoded >> 1) ^ (~(encoded & 1u) + 1u));
				return true;
			}
		}
		return fail("wide integer overflow in binary IR module");
	}

	bool f64(double *r_out) {
		if (size_ - at_ < 8) {
			return fail("truncated binary IR module");
		}
		uint64_t bits = 0;
		for (int i = 0; i < 8; i++) {
			bits |= static_cast<uint64_t>(data_[at_++]) << (i * 8);
		}
		std::memcpy(r_out, &bits, sizeof(bits));
		return true;
	}

	bool kind(Kind *r_out) {
		uint8_t raw = 0;
		if (!byte(&raw) || raw > KIND_MAX) {
			return fail("invalid register kind in binary IR module");
		}
		*r_out = static_cast<Kind>(raw);
		return true;
	}

	bool text(std::string *r_out) {
		uint32_t length = 0;
		if (!u32(&length) || !check_count(length)) {
			return false;
		}
		r_out->assign(reinterpret_cast<const char *>(data_ + at_), length);
		at_ += length;
		return true;
	}

	bool u32_list(std::vector<uint32_t> *r_out) {
		uint32_t count = 0;
		if (!u32(&count) || !check_count(count)) {
			return false;
		}
		r_out->resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!u32(&(*r_out)[i])) {
				return false;
			}
		}
		return true;
	}

	bool text_list(std::vector<std::string> *r_out) {
		uint32_t count = 0;
		if (!u32(&count) || !check_count(count)) {
			return false;
		}
		r_out->resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!text(&(*r_out)[i])) {
				return false;
			}
		}
		return true;
	}

	bool constant(Constant *r_out) {
		uint8_t form = 0;
		if (!byte(&form) || form > FORM_MAX) {
			return fail("invalid constant form in binary IR module");
		}
		r_out->form = static_cast<Constant::Form>(form);
		if (r_out->form == Constant::Form::Number || r_out->form == Constant::Form::Boolean) {
			return f64(&r_out->number);
		}
		if (r_out->form == Constant::Form::Text) {
			return text(&r_out->text);
		}
		if (r_out->form == Constant::Form::Value) {
			uint32_t count = 0;
			if (!text(&r_out->text) || !u32(&count) || count > (size_ - at_) / 8) {
				return fail("truncated constant value in binary IR module");
			}
			r_out->parts.resize(count);
			for (uint32_t i = 0; i < count; i++) {
				if (!f64(&r_out->parts[i])) {
					return false;
				}
			}
		}
		return true;
	}

	bool field(Field *r_out) {
		uint8_t bits = 0;
		if (!text(&r_out->name) || !kind(&r_out->kind) || !u32(&r_out->klass) || !byte(&bits) ||
				!text(&r_out->engine_type) || !constant(&r_out->first)) {
			return false;
		}
		r_out->starts_null = (bits & 1u) != 0;
		r_out->is_public = (bits & 2u) != 0;
		r_out->is_protected = (bits & 4u) != 0;
		r_out->constant = (bits & 8u) != 0;
		r_out->exported = (bits & 16u) != 0;
		return true;
	}

	bool klass(Class *r_out) {
		uint8_t bare = 0;
		uint32_t bits = 0;
		uint32_t count = 0;
		if (!text(&r_out->name) || !u32(&r_out->base) || !kind(&r_out->element) || !u32(&r_out->element_check) ||
				!text(&r_out->element_type) || !byte(&bare) || bare > BARE_MAX || !u32(&bits)) {
			return fail("invalid class header in binary IR module");
		}
		r_out->element_bare = static_cast<Bare>(bare);
		r_out->element_zero = (bits & 1u) != 0;
		r_out->element_counted = (bits & 2u) != 0;
		r_out->array = (bits & 4u) != 0;
		r_out->table = (bits & 8u) != 0;
		r_out->dictionary = (bits & 16u) != 0;
		r_out->built_in = (bits & 32u) != 0;
		r_out->counted = (bits & 64u) != 0;
		r_out->is_interface = (bits & 128u) != 0;
		if (!u32(&count) || !check_count(count, 6)) {
			return false;
		}
		r_out->fields.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!field(&r_out->fields[i])) {
				return false;
			}
		}
		if (!u32_list(&r_out->methods) || !u32(&count) || !check_count(count, 2)) {
			return false;
		}
		r_out->named_methods.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!text(&r_out->named_methods[i].first) || !u32(&r_out->named_methods[i].second)) {
				return false;
			}
		}
		if (!u32(&r_out->constructor) || !u32(&r_out->static_init) || !u32(&r_out->to_string) ||
				!text(&r_out->native_base) || !u32(&count) || !check_count(count, 2)) {
			return false;
		}
		r_out->virtuals.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!text(&r_out->virtuals[i].first) || !u32(&r_out->virtuals[i].second)) {
				return false;
			}
		}
		return u32_list(&r_out->interfaces) && u32_list(&r_out->interface_methods) && text_list(&r_out->signals);
	}

	bool function(Function *r_out, uint8_t p_last_op) {
		uint8_t bits = 0;
		uint32_t count = 0;
		if (!text(&r_out->name) || !u32(&r_out->file) || !u32(&r_out->position) || !u32(&r_out->owner) ||
				!u32(&r_out->written_in) || !byte(&bits) || !u32(&r_out->parameters) || !u32(&r_out->required) ||
				!kind(&r_out->result) || !text_list(&r_out->engine_types) || !u32_list(&r_out->parameter_classes)) {
			return false;
		}
		r_out->is_static = (bits & 1u) != 0;
		r_out->is_public = (bits & 2u) != 0;
		r_out->rest = (bits & 4u) != 0;
		r_out->takes_extra = (bits & 8u) != 0;
		r_out->bound_engine_method = (bits & 16u) != 0;
		if (!u32(&count) || !check_count(count, 2)) {
			return false;
		}
		r_out->virtual_receivers.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!u32(&r_out->virtual_receivers[i].first) || !u32(&r_out->virtual_receivers[i].second)) {
				return false;
			}
		}
		if (!u32(&count) || !check_count(count)) {
			return false;
		}
		r_out->registers.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!kind(&r_out->registers[i])) {
				return false;
			}
		}
		if (!u32(&count) || !check_count(count, 5)) {
			return false;
		}
		r_out->code.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			uint8_t op = 0;
			Instruction &in = r_out->code[i];
			if (!byte(&op) || op > p_last_op || !u32(&in.a) || !u32(&in.b) || !u32(&in.c) || !u32(&in.position)) {
				return fail("invalid instruction in binary IR module");
			}
			in.op = static_cast<Op>(op);
		}
		if (!u32(&count) || !check_count(count)) {
			return false;
		}
		r_out->argument_lists.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!u32_list(&r_out->argument_lists[i])) {
				return false;
			}
		}
		if (!u32(&count) || !check_count(count, 2)) {
			return false;
		}
		r_out->locals.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			if (!text(&r_out->locals[i].name) || !u32(&r_out->locals[i].reg)) {
				return false;
			}
		}
		// Earlier modules identify a generated instance forwarder by its synthetic receiver.
		if (!r_out->is_static && r_out->owner == NONE && !r_out->locals.empty() && r_out->locals.front().reg == 0 &&
				r_out->locals.front().name == BOUND_RECEIVER) {
			r_out->bound_engine_method = true;
		}
		if (!u32(&count) || !check_count(count, 6)) {
			return false;
		}
		r_out->handlers.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			Handler &h = r_out->handlers[i];
			if (!u32(&h.start) || !u32(&h.end) || !u32(&h.target) || !u32(&h.reg) || !u32(&h.type) || !flag(&h.rethrows)) {
				return false;
			}
		}
		return true;
	}
};

// Field order is part of version 1 of the on-disk format.
constexpr uint32_t Module::*MODULE_INDICES[] = {
	&Module::entry_class, &Module::error_class, &Module::type_error_class, &Module::range_error_class,
	&Module::argument_error_class, &Module::function_class, &Module::class_class, &Module::library_function,
	&Module::array_class, &Module::object_class, &Module::dictionary_class, &Module::equal_operator, &Module::add_operator,
	&Module::less_operator, &Module::less_equal_operator, &Module::greater_operator, &Module::greater_equal_operator,
	&Module::subtract_operator, &Module::multiply_operator, &Module::divide_operator, &Module::modulo_operator,
	&Module::negate_operator, &Module::reference_error_class, &Module::regexp_class, &Module::date_class,
	&Module::syntax_error_class, &Module::uri_error_class
};

} // namespace

std::vector<uint8_t> write_binary(const Module &p_module) {
	Writer w;
	for (uint8_t b : MAGIC) {
		w.byte(b);
	}
	w.u32(BINARY_VERSION);
	w.text_list(p_module.files);
	w.u32(static_cast<uint32_t>(p_module.lines.size()));
	for (const auto &file_lines : p_module.lines) {
		w.u32_list(file_lines);
	}
	w.u32(static_cast<uint32_t>(p_module.classes.size()));
	for (const Class &c : p_module.classes) {
		w.klass(c);
	}
	w.u32(static_cast<uint32_t>(p_module.functions.size()));
	for (const Function &f : p_module.functions) {
		w.function(f);
	}
	w.u32(static_cast<uint32_t>(p_module.globals.size()));
	for (const Global &g : p_module.globals) {
		w.text(g.name);
		w.byte(static_cast<uint8_t>(g.kind));
		w.u32(g.klass);
		w.byte((g.starts_null ? 1u : 0u) | (g.is_public ? 2u : 0u) | (g.is_const ? 4u : 0u));
		w.text(g.engine_type);
	}
	w.text_list(p_module.strings);
	w.u32(static_cast<uint32_t>(p_module.numbers.size()));
	for (double n : p_module.numbers) {
		w.f64(n);
	}
	w.u32(static_cast<uint32_t>(p_module.wides.size()));
	for (int64_t wide : p_module.wides) {
		w.i64(wide);
	}
	w.u32(static_cast<uint32_t>(p_module.engine_names.size()));
	for (const EngineName &en : p_module.engine_names) {
		w.u32(en.id);
		w.text(en.key);
		w.text(en.value);
		w.text(en.name);
		w.text(en.owner);
	}
	w.u32_list(p_module.initializers);
	w.text_list(p_module.interface_methods);
	for (auto field : MODULE_INDICES) {
		w.u32(p_module.*field);
	}
	return w.take();
}

// A stripped version-one forwarder still loads its captured receiver from element zero
// and calls the engine method named by its synthetic function name.
static bool stripped_forwarder(const Function &p_fn, const Module &p_module) {
	if (p_fn.is_static || p_fn.owner != NONE || !p_fn.locals.empty() || p_fn.name.compare(0, 6, "godot.") != 0 || p_fn.code.size() < 4) {
		return false;
	}
	const auto &zero = p_fn.code[0], &get = p_fn.code[1], &convert = p_fn.code[2];
	if (zero.op != Op::ConstI32 || zero.b != 0 || get.op != Op::VecGet || get.b != 0 || get.c != zero.a ||
			convert.op != Op::FromAny || convert.b != get.a) {
		return false;
	}
	std::string key = p_fn.name.substr(6);
	const size_t slash = key.find('/');
	if (slash == std::string::npos) {
		return false;
	}
	key[slash] = '.';
	for (const auto &in : p_fn.code) {
		if (in.op == Op::HostCall && in.b < p_module.engine_names.size() && p_module.engine_names[in.b].key == key &&
				in.c < p_fn.argument_lists.size() && !p_fn.argument_lists[in.c].empty() && p_fn.argument_lists[in.c].front() == convert.a) {
			return true;
		}
	}
	return false;
}

std::optional<std::string> read_binary(const uint8_t *p_data, size_t p_size, Module *r_module) {
	if (!p_data || p_size < 4 || std::memcmp(p_data, MAGIC, 4) != 0) {
		return std::string("not a binary IR module");
	}
	Reader r(p_data + 4, p_size - 4);
	uint32_t version = 0;
	if (!r.u32(&version)) {
		return r.error();
	}
	if (version != 1 && version != BINARY_VERSION) {
		return "unsupported binary IR version " + std::to_string(version);
	}
	Module module;
	uint32_t count = 0;
	if (!r.text_list(&module.files) || !r.u32(&count) || !r.check_count(count, 1)) {
		return r.error();
	}
	module.lines.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		if (!r.u32_list(&module.lines[i])) {
			return r.error();
		}
	}
	if (!r.u32(&count) || !r.check_count(count, 18)) {
		return r.error();
	}
	module.classes.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		if (!r.klass(&module.classes[i])) {
			return r.error();
		}
	}
	if (!r.u32(&count) || !r.check_count(count, 17)) {
		return r.error();
	}
	module.functions.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		if (!r.function(&module.functions[i], version == 1 ? static_cast<uint8_t>(Op::HostBindMethod) : OP_MAX)) {
			return r.error();
		}
	}
	if (!r.u32(&count) || !r.check_count(count, 5)) {
		return r.error();
	}
	module.globals.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		Global &g = module.globals[i];
		uint8_t bits = 0;
		if (!r.text(&g.name) || !r.kind(&g.kind) || !r.u32(&g.klass) || !r.byte(&bits) || !r.text(&g.engine_type)) {
			return r.error();
		}
		g.starts_null = (bits & 1u) != 0;
		g.is_public = (bits & 2u) != 0;
		g.is_const = (bits & 4u) != 0;
	}
	if (!r.text_list(&module.strings) || !r.u32(&count) || !r.check_count(count, 8)) {
		return r.error();
	}
	module.numbers.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		if (!r.f64(&module.numbers[i])) {
			return r.error();
		}
	}
	if (!r.u32(&count) || !r.check_count(count, 1)) {
		return r.error();
	}
	module.wides.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		if (!r.i64(&module.wides[i])) {
			return r.error();
		}
	}
	if (!r.u32(&count) || !r.check_count(count, 5)) {
		return r.error();
	}
	module.engine_names.resize(count);
	for (uint32_t i = 0; i < count; i++) {
		EngineName &en = module.engine_names[i];
		if (!r.u32(&en.id) || !r.text(&en.key) || !r.text(&en.value) || !r.text(&en.name) || !r.text(&en.owner)) {
			return r.error();
		}
	}
	if (!r.u32_list(&module.initializers) || !r.text_list(&module.interface_methods)) {
		return r.error();
	}
	for (auto field : MODULE_INDICES) {
		if (!r.u32(&(module.*field))) {
			return r.error();
		}
	}
	if (!r.at_end()) {
		return std::string("trailing bytes in binary IR module");
	}
	if (!binary_references_valid(module)) {
		return std::string("invalid reference in binary IR module");
	}
	for (auto &fn : module.functions) {
		fn.bound_engine_method = fn.bound_engine_method || stripped_forwarder(fn, module);
		if (version == 1) {
			release_temporaries(fn, true);
		}
	}
	*r_module = std::move(module);
	return std::nullopt;
}

} // namespace ir
} // namespace kas3
