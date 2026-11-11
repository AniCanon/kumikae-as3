#include "host/class_registry.h"

#include <cstring>

namespace as3 {

static const ScriptClass *classes[64];
static int class_count = 0;

void register_class(const ScriptClass *p_class) {
	if (class_count < 64) {
		classes[class_count++] = p_class;
	}
}

const ScriptClass *find_class(const char *p_name) {
	for (int i = 0; i < class_count; i++) {
		if (std::strcmp(classes[i]->name, p_name) == 0) {
			return classes[i];
		}
	}
	return nullptr;
}

} // namespace as3
