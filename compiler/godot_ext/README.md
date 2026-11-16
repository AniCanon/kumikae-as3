# `godot_ext/`: the GDExtension

What makes AS3 a scripting language inside Godot 4: scripts that extend Godot classes, attached to nodes,
called by the engine.

| Files | Job |
|---|---|
| `register.cpp`, `language.h` | Entry point; the script language Godot sees |
| `program` | The project's scripts: found, read (from a folder or a pack), checked, lowered |
| `script`, `instance` | A script resource; one object's instance and the calls Godot makes on it |
| `counted` | A script object whose Godot object is reference-counted: each lives while AS3 or Godot holds it |
| `members` | What Godot reaches of a script by name: public fields to read and write (an Array or a Dictionary as a copy of Godot's), public getters and setters, public methods, and the lists of them with Godot's types |
| `godot_host` | The interpreter's four interfaces, done with Godot's calls; values to and from `Variant` |
| `godot_source.cpp` | Where values come from (`EngineSource`): defaults, constants, constructors, singletons, static methods; and the time (`Clock`: Godot's clock, the C library's zone) |
| `godot_calls.h` | What the two host files share: the arguments of a call, a failed call in AS3's words |
| `godot_utility.cpp` | Godot's global functions |
| `godot_untyped.cpp` | An untyped value as a Variant and back: an integer stays an integer, a whole float a float |
| `godot_collections.cpp` | A vector handed to Godot: the packed array Godot takes there, or an Array; an Object or a Dictionary as a Dictionary, its keys as text |
| `program` (collector) | Once a frame, a step of the cycle collector with a budget of work (`COLLECTOR_WORK`); a whole collection when Godot shuts down (`Project::before_shutdown`, after its main loop, of any kind, has finished) and when the program is unloaded. `GodotHost::node` and `node_edges` say what a reference-counted object holds: its listeners, and its AS3 script's object, joined to it (`counted`: the two are one to the collector) |
| `program` (reload) | Watches the scripts, compiles them again, and moves instances, listeners and script resources onto the new program (`vm/reload`); a container's class the new build no longer names (`Vector.<T>`) is added to it. Also asked for at once: the language's `reload_scripts()`, and Godot's debugger |
| `restart.cpp` | A save that changes code that runs once (`ir::run_once_changes`) starts the running scene again from its file, and says what that does not make again (nodes outside the scene, objects static fields hold, static fields' first values). A save while the scene is being started again starts it once more when it is in |
| `sources` (`KAS3_SOURCE_DIR`) | Where the scripts are read from, and the watch's look at them (a folder is listed again only when it changed). In a debug build, a folder outside the project to read and watch the scripts in: a game on a simulator runs from a pack, and is told where on the computer its scripts are edited. Its scenes and text resources (`.tscn`, `.tres`) are watched there too, and an edited one takes the place of the pack's copy (`take_over_path`); the running scene starts again if its file changed |
| `callable` | A method as a Godot function; listeners as the cycle collector sees them (`vm/collector.h`): taken out of their signals to see whether anything else holds them, and put back in their places if so |

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
- Godot reaches a script's members by name only through `members.h` (`find_member`), so that get, set,
  calls and the lists agree. Every place that runs AS3 on a script object for Godot calls
  `hold_if_referenced` after it (`instance.h`): that is how a reference-counted object learns AS3 holds it.

Build and test: `cmake --build compiler/build-godot -j8 && python3 compiler/tests/run_godot.py compiler/tests`
(needs `godot` 4.7 on the `PATH`). iOS: `tests/build_ios.sh`.
