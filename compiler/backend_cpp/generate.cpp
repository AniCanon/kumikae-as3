#include "generate.h"

#include "facts.h"
#include "function_writer.h"
#include "names.h"
#include "program_writer.h"
#include "support.h"
#include "thunks.h"

#include <set>

namespace kas3 {
namespace backend_cpp {

namespace {

constexpr std::string_view HEADER = "program.h";
constexpr std::string_view TABLE = "program";

std::string opening() {
	return "#include \"" + std::string(HEADER) + "\"\n\nnamespace " + std::string(PROGRAM_NAMESPACE) + " {\n\n";
}

} // namespace

Generated generate(const ir::Module &p_module, const Options &p_options) {
	Generated out;
	if (std::optional<std::string> missing = first_unsupported(p_module)) {
		out.unsupported = std::move(*missing);
		return out;
	}
	ThunkTable thunks;
	const bool keeps_stack = reads_error_stack(p_module);
	const Facts facts = find_facts(p_module, keeps_stack);
	std::vector<std::string> by_class(p_module.classes.size());
	std::string of_no_class;
	for (uint32_t i = 0; i < p_module.functions.size(); i++) {
		const uint32_t owner = p_module.functions[i].owner;
		(owner == ir::NONE ? of_no_class : by_class[owner]) += FunctionWriter(p_module, i, thunks, keeps_stack, &facts).write();
	}
	out.files.push_back(File{ std::string(HEADER), write_header(p_module) });
	// Two classes of one name in two packages, or one called `program`, still get a file each.
	std::set<std::string> taken = { std::string(TABLE) };
	std::string of_classes;
	for (uint32_t i = 0; i < p_module.classes.size(); i++) {
		if (by_class[i].empty()) {
			continue;
		}
		std::string name = identifier(p_module.classes[i].name);
		if (!taken.insert(name).second) {
			name += "_" + std::to_string(i);
			taken.insert(name);
		}
		if (p_options.one_unit) {
			of_classes += by_class[i];
			out.not_written.push_back(name + ".cpp");
			continue;
		}
		out.files.push_back(File{ name + ".cpp", generated_notice("the class " + p_module.classes[i].name) + opening() + by_class[i] + "} // namespace " +
											std::string(PROGRAM_NAMESPACE) + "\n" });
	}
	out.files.push_back(File{ std::string(TABLE) + ".cpp",
			generated_notice("the whole program") + opening() + of_classes + of_no_class + write_table(p_module, thunks, keeps_stack) });
	return out;
}

} // namespace backend_cpp
} // namespace kas3
