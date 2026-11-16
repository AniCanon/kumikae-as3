// A String's methods that may run the program or raise, as functions of plain values (vm/library.h).
#include "library.h"

#include "any.h"
#include "text_units.h"
#include "uri_text.h"

#include <optional>
#include <vector>

namespace kas3 {
namespace vm {

using ir::StringFunction;

namespace {

// The pieces split() gives of a text by a delimiter that is text: the whole of it with none (p_whole), each
// UTF-16 unit for an empty one; at most p_most pieces.
std::vector<std::string> split_text(std::string_view p_self, bool p_whole, std::string_view p_by, uint32_t p_most) {
	const uint32_t length = static_cast<uint32_t>(utf16_length(p_self));
	std::vector<std::string> pieces;
	if (p_whole) {
		pieces.emplace_back(p_self);
	} else if (p_by.empty()) {
		for (uint32_t i = 0; i < length && pieces.size() < p_most; i++) {
			pieces.push_back(units_between(p_self, i, i + 1));
		}
	} else if (length > 0) {
		const uint32_t step = static_cast<uint32_t>(utf16_length(p_by));
		uint32_t from = 0;
		while (pieces.size() < p_most) {
			const int32_t found = find_units(p_self, p_by, from);
			const uint32_t end = found < 0 ? length : static_cast<uint32_t>(found);
			pieces.push_back(units_between(p_self, from, end));
			if (found < 0) {
				break;
			}
			from = end + step;
		}
	} else {
		pieces.emplace_back(); // An empty string with a delimiter: one empty piece.
	}
	if (pieces.size() > p_most) {
		pieces.resize(p_most);
	}
	return pieces;
}

// split(delimiter, limit), as ECMAScript 3 has it: no delimiter (null or undefined) is the whole string;
// an empty one gives each UTF-16 unit; at most `limit` pieces. A delimiter that is neither a String nor
// null becomes its text, which may run the program.
bool split(Library &p_library, const Slot *p_values, Slot *r_out) {
	const AnyTag tag = any::tag(p_values[1]);
	const bool whole = tag == AnyTag::Null || tag == AnyTag::Undefined;
	std::string by;
	if (!whole && !p_library.runner.text(p_values[1], &by)) {
		return false;
	}
	const Str *string = static_cast<const Str *>(p_values[0].p);
	const std::vector<std::string> pieces = split_text(string ? string->view() : std::string_view(), whole, by, to_uint32(p_values[2].d));
	Vector *made = p_library.fresh_vector(p_library.module().array_class, pieces.size());
	for (size_t i = 0; made && i < pieces.size(); i++) {
		Str *piece = p_library.text(pieces[i]);
		if (!piece) {
			p_library.heap.release_object(made);
			return false;
		}
		made->data[i] = any::string(piece);
	}
	r_out->bits = 0;
	r_out->p = made;
	return made != nullptr;
}

constexpr std::string_view URI_FUNCTIONS[] = { "encodeURI", "encodeURIComponent", "decodeURI", "decodeURIComponent" };

// The URI functions. One that can't read or write its text is AS3's URIError #1052.
bool uri(Library &p_library, StringFunction p_which, std::string_view p_text, Slot *r_out) {
	const size_t which = static_cast<size_t>(p_which) - static_cast<size_t>(StringFunction::EncodeURI);
	const bool component = p_which == StringFunction::EncodeURIComponent || p_which == StringFunction::DecodeURIComponent;
	const std::optional<std::string> made = which < 2 ? encode_uri(p_text, component) : decode_uri(p_text, component);
	if (!made) {
		const ir::Module &module = p_library.module();
		p_library.runner.raise(module.uri_error_class != ir::NONE ? module.uri_error_class : module.error_class, uri_error(URI_FUNCTIONS[which]));
		return false;
	}
	r_out->bits = 0;
	r_out->p = p_library.text(*made);
	return r_out->p != nullptr;
}

} // namespace

bool text_call(Library &p_library, StringFunction p_function, const Slot *p_values, size_t p_count, Slot *r_out) {
	if (p_function == StringFunction::Split) {
		return p_count == 3 && split(p_library, p_values, r_out);
	}
	const Str *string = p_count > 0 ? static_cast<const Str *>(p_values[0].p) : nullptr;
	return uri(p_library, p_function, string ? string->view() : std::string_view("null"), r_out);
}

} // namespace vm
} // namespace kas3
