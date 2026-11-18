// Play builds the project's program library by itself: when a game is run from its folder, the scripts as
// they are now are written as C++ (backend_cpp), built by the system's C++ compiler into a library under
// the project's `.godot/kas3`, and loaded. The compiler runs beside the game, never in its way: a run that
// finds no library for its scripts starts on the interpreter and says so, and the next run is native
// (godot_ext/play_cache.h says what is kept where; godot_ext/README.md, when this is done and when not).
#pragma once

#include "ir.h"

#include <cstddef>
#include <string>

namespace kas3 {
namespace play {

// The headers generated code is built against, carried by the extension (tools/embed_runtime.py), so that
// a library is always built for the layout of the runtime that loads it, wherever the extension is
// installed; and one word for those headers and the backend together.
struct RuntimeHeader {
	const char *name;
	const char *text;
};
extern const RuntimeHeader RUNTIME_HEADERS[];
extern const size_t RUNTIME_HEADER_COUNT;
extern const char *const BUILD_IDENTITY;

// The library Play built for these scripts, by its path, if it is there. Empty otherwise, and then, where
// Play builds (below), a build of it has been started and said, or why there is none has been said: once.
std::string library_for(const ir::Module &p_module);
// The scripts were reloaded into a running game: the library of what they are now is built for the next
// run. Nothing is said unless it fails.
void build_ahead(const ir::Module &p_module);
// Once a frame: what was written is handed to the compiler, and whether the build this run started has
// ended is said in one line.
void poll();
// The game is ending: what is being written is waited for and its build started, so that a game closed at
// once still leaves the next run its library. The compiler itself is not waited for.
void finish();
// Whether Play builds here at all: the switch is on, a game is running (not the editor) from a project's
// folder, on a system whose shell and compiler this knows how to run.
bool builds();

} // namespace play
} // namespace kas3
