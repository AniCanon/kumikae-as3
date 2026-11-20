# `godot_ext/`: the Godot extension

The extension is a GDExtension that registers ActionScript 3 as a scripting language in stock Godot 4.7.
A `.as` file is then a script: it extends a Godot class, attaches to a node, and is called by the engine.
For how that looks to someone writing scripts, read [Using AS3 in Godot](../../docs/godot.md).

**Depends on:** `runtime/`, `vm/`, `backend_cpp/`, `ir/` and `src/`, and
[godot-cpp](https://github.com/godotengine/godot-cpp) for Godot 4.7. This is the only folder that includes
godot-cpp. Nothing here is included by another folder.

## Build and test

```sh
cmake -S compiler -B compiler/build-godot -DKAS3_GODOT_CPP=<path to godot-cpp>
cmake --build compiler/build-godot --target kas3_godot
python3 compiler/tests/run_godot.py compiler/tests compiler/build/kas3c
```

The library is written to `tests/godot_project/bin`. The tests need `godot` 4.7 on the `PATH`, or its path
in `KAS3_GODOT`. For iOS, run `tests/build_ios.sh`. For the Web (WebAssembly), run `tests/build_web.sh`.

godot-cpp is built as `template_release`, and Godot's editor loads that library too. godot-cpp compiles
nothing differently for the editor, so one library serves both.

## Files

| Files | Job |
|---|---|
| `register.cpp`, `language.h` | The entry point, and the script language Godot sees |
| `program` | The project's scripts: found, read, checked and lowered. Also the reload, and the collector's step each frame |
| `sources`, `watch.h`, `test_folders.h` | Where scripts are read from, when a file is read again, which folders hold tests, and extra `KAS3_TEST_SOURCES` directories loaded when `KAS3_TESTS` is set |
| `script`, `instance` | A script resource, and one object's script instance |
| `script_call.cpp` | Godot calling a function of a script object: arguments counted, checked and converted |
| `members` | What Godot reaches of a script by name: public fields, getters, setters and methods |
| `counted` | Script objects whose Godot object is reference counted, and a Godot object arriving where a script's class is declared |
| `godot_host`, `godot_handles.cpp`, `handles.h` | The interpreter's four host interfaces, implemented with Godot's calls. Handles on Godot's values |
| `godot_prepared.cpp`, `worked.h` | Each engine instruction resolved once when a program loads |
| `godot_source.cpp`, `godot_utility.cpp` | Constructors, constants, singletons, static methods, the clock, and Godot's global functions |
| `godot_signals.cpp`, `godot_emit.cpp`, `listeners.cpp` | Listening to a signal, emitting one, and listeners as the cycle collector sees them |
| `godot_untyped.cpp`, `godot_collections.cpp` | Untyped values to and from `Variant`, and AS3 collections handed to Godot |
| `callable`, `bound_method.h` | A method as a Godot callable |
| `restart.cpp` | What a reload reports, and restarting the running scene |
| `native_program`, `shared_library` | [The program library](#the-program-library): found, loaded, checked, and given what it needs from Godot |
| `play_build`, `play_cache.h`, `toolchain.h` | [Building the program library on Play](#building-on-play) |

## How it works

- **One program for each project.** Every `.as` file under the project is compiled together when the first
  script loads. If any fails to compile, the errors are reported once and no script loads.
- **The interpreter doesn't know Godot.** `vm/host.h` is the whole boundary. The extension implements its
  four interfaces with Godot's calls.
- **Godot's names come from Godot.** The generated name table says that `moveAndSlide` is `move_and_slide`.
  Nothing is worked out backward from the AS3 name.
- **Engine instructions are resolved once.** When a program loads, `prepare` finds what each engine
  instruction needs: an operator's evaluator for its operand types, a method bind, a property's accessors,
  a constant's value. Nothing is looked up by name while the program runs. An instruction that meets
  values of other types than expected falls back to a request by name.
- **In the interpreter, a Godot value is boxed** in a handle (a `godot::Variant` the interpreter holds).
  A handle on an object that isn't reference counted, such as a node, is shared by everyone who copies it.
- **Calls can nest.** The engine calls a script, which calls the engine, which can call a script again,
  for as long as the thread's stack lasts. With the stack nearly used up, one more call is Error #1023.
- **The cycle collector runs in steps.** Once a frame it does a bounded amount of work. It runs to
  completion when Godot shuts down and when the program is unloaded. A reference-counted Godot object and
  its AS3 script object are one node to the collector.
- **Scripts are watched in a debug build.** When a file changes, the program is compiled again and the
  running game moves onto it. [Hot reload](../../docs/hot-reload.md) describes what a reload does.
  `vm/reload` does the move.

## Rules

- **A value from Godot is checked against the register's type before it's taken.** What doesn't fit is an
  AS3 error that names what gave it.
- **A name Godot needs is made once and kept** (`GodotHost::name`). No `StringName` is built for a call.
- **Files are read through Godot** (`FileAccess`, `DirAccess`, `res://`), never through the operating
  system's paths, because an exported game keeps its files in a pack. The one exception is the Play build's
  cache, which a C++ compiler reads.
- **AS3 runs on the main thread.** A call from another thread is refused with a message. A script instance
  that Godot frees on another thread is freed on the main thread, at the end of the frame.
- **Godot reaches a script's members by name only through `members.h`** (`find_member`), so that get, set,
  call and the member lists agree.
- **Every place that runs AS3 on a script object for Godot calls `hold_if_referenced` afterward**
  (`instance.h`). That's how a reference-counted object learns that AS3 holds it.
- **Never hand the same `Array` or `Dictionary` to two calls.** Godot may write to one it's given.
- **Every behavior is compared with GDScript's.** In `tests/godot_project/`, a driver prints what the AS3
  script gave beside what GDScript gives, and `tests/run_godot.py` checks that they agree.
- **Godot must not reload the extension while a game runs.** The host keeps a binding on each Godot object
  that generated code reads directly, to learn when the object is freed. The bindings last as long as the
  library does.

## Native iOS and Android exports

`ios_export` registers an editor export plugin for iOS and Android. On macOS, it collects the AS3 paths
selected by an iOS preset and uses the same test-folder rules and source ordering as the game. Once Godot
writes the Xcode project, the plugin compiles one object for the device and one for the Simulator, and Xcode
links the object for its destination into the app. On Android exports, it compiles `libkas3_program.so` for
`arm64-v8a` with the Android NDK (`aarch64-linux-android24-clang++`) and packages it into the APK/AAB, where
`native_program` loads it alongside `libkas3_godot.so`.

The default **Automatic** mode interprets debug exports and compiles release exports. Both presets accept
`actionscript/native_code`: Automatic (2), Interpreter (0) and Native (1). Earlier iOS boolean settings keep
their original meaning. Native debug exports record constructor arguments for hot reload. Release exports
omit that bookkeeping.

## Web export (WebAssembly)

`tests/build_web.sh <path to godot-cpp>` compiles `kas3_godot` with Emscripten as a single-threaded
WebAssembly GDExtension side module (`bin/web/libkas3_godot.wasm`, registered under `web.wasm32` in
`kas3.gdextension`). Export a project with Godot 4.7's **Web** preset with `variant/extensions_support=true`
and `variant/thread_support=false`; scripts run on the interpreter in the browser without requiring
`SharedArrayBuffer` headers. The release link removes unused code and retains the entry point Godot
loads by name. Native ahead-of-time WebAssembly program libraries are future work.

## The program library

A project's scripts, written as C++ by `kas3c cpp --godot` and built into one shared library, are the
project's program library. When the extension loads the scripts, it looks for one:

1. The file that `KAS3_PROGRAM_LIBRARY` names.
2. `bin/libkas3_program.dylib` in the project (`.so` on Linux).
3. A library the extension built itself, under `.godot/kas3`.

The library must have been built from the scripts as they are now, which the extension checks with a
fingerprint of the compiled module, and for this runtime's layout. If it was, the scripts run as native
code and Godot's output says so. If not, a warning says why and the interpreter runs the scripts.

To build one by hand, run this from the project's folder:

```sh
kas3c cpp --godot --unit --remember $(find . -name '*.as' -not -path '*/.*' -not -path './tests/*' | sed 's|^\./||' | LC_ALL=C sort) -o native/
c++ -std=c++17 -O2 -fno-exceptions -fno-rtti -ffp-contract=off -fno-unroll-loops -fvisibility=hidden \
    -I <repository>/compiler/runtime -I <repository>/compiler/vm -I <repository>/compiler/ir \
    -I <repository>/compiler/src -shared native/*.cpp -o bin/libkas3_program.dylib
```

Give the same files the extension reads, in sorted order: every `.as` file except those in folders whose
name starts with a dot, folders marked with `.gdignore`, and the project's test folders. Both must compile the same module. Leave
`--remember` out for a library you ship.

The flags for Linux are in `play_cache.h`. Neither Linux nor Windows has been built or run.

With a program library loaded, everything else in this folder works as before. The runtime's host keeps an
interpreter over the same module, in which each function's body is native code. A call from Godot runs the
generated function itself. See [runtime/README.md](../runtime/README.md).

One thing doesn't work under native code yet:

- **Hot reload of native code.** An edit moves the running game onto the interpreter, with its state.

Objects created as native code are updated in place by the reloads that follow, as the interpreter's own
are, when the library was written with `kas3c cpp --remember`. The library Play builds always is. In a debug
build the host then remembers each object's constructor arguments
([runtime/README.md](../runtime/README.md#what-objects-are-constructed-with)).

The time limit on a call from the engine (Error #1502) is the project's setting either way: the extension
hands it to the interpreter and to the host of a program library alike.

### Building on Play

With the project setting `actionscript/native/play` on, the extension builds the program library itself
when a game runs from its project folder. The setting is on by default on macOS, where the build has been
run, and off elsewhere.

- **The key.** A build is identified by the scripts as compiled, the runtime's layout, the backend and the
  runtime headers this extension carries, the C++ compiler, and the flags. If `.godot/kas3` has
  `libkas3_program_<key>`, the extension loads it. No compiler runs.
- **A build.** Without that library, the game starts on the interpreter and says so. The scripts are
  written as C++ on a thread, and the compiler runs beside the game at a lower priority. When it ends, the
  game says the next run is native. If the game closes first, the compiler keeps running, so the next run
  finds the library.
- **No path to this repository.** The extension carries the backend and the runtime's headers
  (`tools/embed_runtime.py`). The library is always built for the runtime that loads it, wherever the
  extension is installed.
- **The cache.** `.godot/kas3` holds, for each build, a folder `<key>/` with the C++ and `build.log`, the
  library, a `<key>.building` marker while it's built, and a `<key>.failed` marker if it couldn't be. A
  library is never overwritten. When a build begins, old builds are removed, except those in use and the
  three newest others.
- **No compiler.** One warning names what to install, and the interpreter runs the scripts. On macOS the
  extension looks for Xcode's command line tools itself and never runs `/usr/bin/c++` without them,
  because that would open an install window. `KAS3_CXX` names another compiler.
- **A failed build** is reported once, with the path of the compiler's output, and isn't tried again.
  Delete `.godot/kas3` to try again.
- **Nothing waits for the compiler.** `play_build` starts it and checks once a frame whether it has ended.
- **Where nothing is built:** in the editor, in a game run from a pack (an export, the iOS Simulator), on
  a system other than macOS and Linux, and in a project with a library given by hand. The Linux path is
  written and hasn't been built or run.

`play_cache.h` and `toolchain.h` include nothing from Godot, so `tests/test_play_cache.cpp` tests the rules
with folders of its own. `tests/play_godot.py` plays a project in Godot: twice, edited between runs, with
no compiler, and with a compiler that fails.

`KAS3_NATIVE_PLAY` overrides the project setting: `1` for on, `0` for off. The tests set it to `0` for
every run except those that test the build.
