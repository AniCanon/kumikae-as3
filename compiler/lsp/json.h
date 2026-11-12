// A small JSON value, reader and writer for the language server. No exceptions: reading returns false,
// and asking a value for something it is not gives a default.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kas3 {

class Json {
public:
	enum Kind : uint8_t { Null, Bool, Number, String, Array, Object };

private:
	Kind kind_ = Null;
	bool bool_ = false;
	double number_ = 0;
	std::string string_;
	std::vector<Json> items_; // Array elements, or object values.
	std::vector<std::string> keys_; // Object keys, beside items_.

public:
	Json() {}
	Json(bool p_value) :
			kind_(Bool), bool_(p_value) {}
	Json(double p_value) :
			kind_(Number), number_(p_value) {}
	Json(int p_value) :
			kind_(Number), number_(p_value) {}
	Json(uint32_t p_value) :
			kind_(Number), number_(p_value) {}
	Json(int64_t p_value) :
			kind_(Number), number_((double)p_value) {}
	Json(const char *p_value) :
			kind_(String), string_(p_value) {}
	Json(std::string p_value) :
			kind_(String), string_(std::move(p_value)) {}
	Json(std::string_view p_value) :
			kind_(String), string_(p_value) {}
	static Json array() {
		Json value;
		value.kind_ = Array;
		return value;
	}
	static Json object() {
		Json value;
		value.kind_ = Object;
		return value;
	}

	Kind kind() const { return kind_; }
	bool is_null() const { return kind_ == Null; }
	bool is_string() const { return kind_ == String; }
	bool is_number() const { return kind_ == Number; }
	bool is_object() const { return kind_ == Object; }
	bool is_array() const { return kind_ == Array; }

	bool boolean(bool p_default = false) const { return kind_ == Bool ? bool_ : p_default; }
	double number(double p_default = 0) const { return kind_ == Number ? number_ : p_default; }
	// A whole number in the range of int64, otherwise the default.
	int64_t integer(int64_t p_default = 0) const;
	const std::string &string() const; // Empty unless a string.
	size_t size() const { return items_.size(); } // Elements or members.
	const Json &at(size_t p_index) const; // Null when out of range.
	const Json &get(std::string_view p_key) const; // Null when missing, or not an object.
	bool has(std::string_view p_key) const;
	const std::vector<Json> &items() const { return items_; }
	const std::vector<std::string> &keys() const { return keys_; }

	// For an object: adds the member, or replaces it. Returns this.
	Json &set(std::string_view p_key, Json p_value);
	// For an object that is being read: adds the member without looking for the key. A key given twice
	// is then there twice, and get() answers with the last.
	void append(std::string p_key, Json p_value) {
		keys_.push_back(std::move(p_key));
		items_.push_back(std::move(p_value));
	}
	// For an array. Returns this.
	Json &push(Json p_value);
};

// False if the text is not one JSON value. Nesting deeper than 200 is refused.
bool parse_json(std::string_view p_text, Json *r_value);
// Compact, and always valid UTF-8: bytes that are not are written as U+FFFD.
std::string write_json(const Json &p_value);

} // namespace kas3
