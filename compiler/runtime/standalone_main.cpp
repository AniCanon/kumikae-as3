// A program library as a plain executable: what the native oracle (tests/run_native.py) builds and runs.
#include "native_host.h"

int main() {
	return kas3::rt::run_standalone(kas3_program());
}
