# `godot_ext/`: the GDExtension

What makes AS3 a scripting language inside Godot 4: scripts that extend Godot classes, attached to nodes,
called by the engine.

| Files | Job |
|---|---|
| `register.cpp`, `language.h` | Entry point; the script language Godot sees |
| `program` | The project's scripts: found, read (from a folder or a pack), checked, lowered |
| `script`, `instance` | A script resource; one object's instance and the calls Godot makes on it |
| `godot_host` | The interpreter's three interfaces, done with Godot's calls; values to and from `Variant` |
| `godot_source.cpp` | Where values come from (`EngineSource`): defaults, constants, constructors, singletons, static methods |
| `godot_calls.h` | What the two host files share: the arguments of a call, a failed call in AS3's words |
| `godot_utility.cpp` | Godot's global functions |
| `godot_untyped.cpp` | An untyped value as a Variant and back: an integer stays an integer, a whole float a float |
| `godot_collections.cpp` | A vector handed to Godot: the packed array Godot takes there, or an Array; an Object or a Dictionary as a Dictionary, its keys as text |
| `program` (reload) | Watches the scripts, compiles them again, and moves instances, listeners and script resources onto the new program (`vm/reload`) |
| `program` (restart) | A save that changes code that runs once (`ir::run_once_changes`) starts the running scene again from its file |
| `program` (`KAS3_SOURCE_DIR`) | In a debug build, a folder outside the project to read and watch the scripts in: a game on a simulator runs from a pack, and is told where on the computer its scripts are edited |
| `callable` | A method as a Godot function; the look for listener loops |

## Rules

- **The only folder that includes godot-cpp.** Nothing here is included by `src/`, `ir/` or `vm/`.
- **A value from Godot is checked against the register's type before it is taken** (`arrives`, `take`).
  What does not fit is an AS3 error that names what gave it.
- **A name Godot needs is made once and kept** (`GodotHost::name`). No `StringName` is built per call.
- **Files are read through Godot** (`FileAccess`, `DirAccess`, `res://`), never the operating system's
  paths: an exported game keeps them in a pack.
- **AS3 runs on the main thread.** A call from another thread is refused with a message.
- **Every behaviour is compared with GDScript's** in `tests/godot_project/`: the driver prints what the AS3
  script gave beside what GDScript gives, and `tests/run_godot.py` checks they agree.
- An Array or a Dictionary handed to Godot may be written by it: never hand the same one to two calls.

Build and test: `cmake --build compiler/build-godot -j8 && python3 compiler/tests/run_godot.py compiler/tests`
(needs `godot` 4.7 on the `PATH`). iOS: `tests/build_ios.sh`.
