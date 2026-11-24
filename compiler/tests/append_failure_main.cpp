// The native append fixture can fail its marked append without exhausting the host process.
#include "native_host.h"
#include "api.h"

namespace rt = kas3::rt;
namespace vm = kas3::vm;

int main(int argc, char **) {
	if (argc == 1) {
		return rt::run_standalone(kas3_program());
	}
	vm::StandaloneOutput output;
	vm::SystemClock clock;
	{
		rt::Embedder embedder{ output, vm::NoEngine::instance(), vm::NoEngine::instance(), clock };
		rt::Host host(*kas3_program(), embedder);
		rt::Api api = *host.context.api;
		api.append_text_owned = [](rt::Context *p_context, vm::Str **r_prefix, const vm::Str *p_piece) {
			if (p_piece && p_piece->view() == "/retry") {
				vm::fail_allocations(1);
			}
			return rt::runtime_api()->append_text_owned(p_context, r_prefix, p_piece);
		};
		host.context.api = &api;
		if (!host.run()) {
			return 1;
		}
	}
	return vm::live_strings() || vm::live_objects() ? 3 : 0;
}
