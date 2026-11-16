#include "support.h"

namespace kas3 {
namespace backend_cpp {

namespace {

bool translated(ir::Op) {
	return false;
}

} // namespace

std::optional<std::string> first_unsupported(const ir::Module &p_module) {
	for (const ir::Function &function : p_module.functions) {
		for (const ir::Instruction &in : function.code) {
			if (!translated(in.op)) {
				return std::string(ir::op_name(in.op));
			}
		}
	}
	return std::nullopt;
}

} // namespace backend_cpp
} // namespace kas3
