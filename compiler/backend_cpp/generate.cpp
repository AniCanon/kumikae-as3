#include "generate.h"

#include "support.h"

namespace kas3 {
namespace backend_cpp {

Generated generate(const ir::Module &p_module) {
	Generated out;
	if (std::optional<std::string> missing = first_unsupported(p_module)) {
		out.unsupported = std::move(*missing);
	}
	return out;
}

} // namespace backend_cpp
} // namespace kas3
