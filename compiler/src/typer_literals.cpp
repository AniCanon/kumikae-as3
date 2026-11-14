#include "typer.h"

namespace kas3 {

const Type *Typer::number_literal(const NumberLit *p_literal) const {
	unsigned long long value = 0;
	if (!whole_value(p_literal, &value)) {
		return number_;
	}
	if (value <= 2147483647ull) {
		return int_;
	}
	return value <= 4294967295ull ? uint_ : number_;
}

bool Typer::whole_value(const NumberLit *p_literal, unsigned long long *r_value) const {
	std::string text(p_literal->text);
	bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
	if (!hex && text.find_first_of(".eE") != std::string::npos) {
		return false;
	}
	size_t start = hex ? 2 : 0;
	while (start + 1 < text.size() && text[start] == '0') {
		start++;
	}
	std::string digits = text.substr(start);
	if (digits.empty() || digits.size() > 15) {
		return false;
	}
	char *end = nullptr;
	*r_value = std::strtoull(digits.c_str(), &end, hex ? 16 : 10);
	return end && *end == 0;
}

} // namespace kas3
