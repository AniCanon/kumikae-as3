#include "host.h"

#include <cstdio>

namespace kas3 {
namespace vm {

void StandaloneOutput::print(std::string_view p_text) {
	std::fwrite(p_text.data(), 1, p_text.size(), stdout);
	std::fputc('\n', stdout);
}

void StandaloneOutput::error(std::string_view p_text) {
	std::fflush(stdout);
	std::fwrite(p_text.data(), 1, p_text.size(), stderr);
	std::fputc('\n', stderr);
}

} // namespace vm
} // namespace kas3
