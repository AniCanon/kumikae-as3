# Editors

`kas3c lsp` is a language server: an editor starts it, and it reports the compiler's errors as you type and
answers go to definition, find references, hover, completion, signature help, outline and symbol search. It is the same program as
`kas3c check`, so the two never disagree.

Build it first:

```sh
cmake -S compiler -B compiler/build
cmake --build compiler/build
```

The server is then `compiler/build/kas3c`. The steps below use its full path; a copy on your `PATH` works
too.

Open the folder that holds `project.godot`, or any folder inside it. Every `.as` file in the project is
part of the program.

> **Editor validation:**
> - **VS Code 1.145:** works. With the client below, VS Code started the server, opened a script that
>   extends `CharacterBody2D`, received its two errors, and received the shorter list after the file changed.
>   This was confirmed from a log of the messages between the two, and the owner saw both errors underlined
>   in the editor.
>   It found one fault, now fixed: VS Code starts a server with `--stdio`, which `kas3c lsp` used to reject.
> - **Rider RD-262.10968.170:** the native plugin passes automated sandbox checks for pairing,
>   completion lookup, Tab/Enter acceptance, undo and changed-document cancellation (2026-11-15).
>   See [completion results](../docs/results/2026-11-completion.md) for measurements and limits.
> - **Free unified IDEA IU-261.22158.277 with LSP4IJ 0.21.0:** automated sandbox checks cover Enter/Tab,
>   snippets, imports, overrides, parameter help, navigation and outline. See the
>   [follow-up results](../docs/results/2026-11-completion-followup.md) for the compatibility matrix.

> **Windows validation is pending.** Binary transport, pipe readiness, UTF-8 filesystem paths and
> `.exe` discovery are implemented, but these runtime branches have not been exercised on Windows.

## VS Code

The client is in [`vscode/`](vscode/). It needs one library from npm.

```sh
cd editors/vscode
npm install
```

Then, in VS Code: **Run > Start Debugging** with the `editors/vscode` folder open starts a second window
with the extension loaded. In that window, open your project and set **Kumikae AS3: Server Path**
(`kas3.serverPath`) to the full path of `kas3c`.

| Setting | What it does |
|---|---|
| `kas3.serverPath` | The `kas3c` program. Default: `kas3c`, found on the `PATH`. |
| `kas3.godot` | Load Godot's API. Default: on. |
| `kas3.declarations` | More declaration files. |

The colouring is a small grammar: keywords, strings, comments and numbers.

## Rider and IntelliJ IDEA Community

The [Kumikae JetBrains plugin](jetbrains/) connects `kas3c lsp` through Rider's native LSP client or
LSP4IJ in IntelliJ IDEA.
It provides AS3 highlighting, completion, argument help, navigation and local bracket/quote assistance,
as well as project/run tools and the Simulator panel. Install the plugin ZIP built from the same
compiler revision; it bundles `kas3c` when a compiler build is available. See the plugin README for the
supported Rider build and build commands.

For IntelliJ IDEA, install and enable **LSP4IJ 0.21.0** from Settings → Plugins, then install Kumikae.
IDEA's [unified distribution](https://www.jetbrains.com/help/idea/intellij-idea-single-distribution.html)
includes the free features that replaced Community Edition; this adapter
does not use an Ultimate API or require a paid subscription. Rider uses its native client and does not
require LSP4IJ. Installing LSP4IJ in Rider does not start a second AS3 server. Android Studio is outside
the scope. See the plugin README for the specific builds checked.

## Any other editor

Anything with a Language Server Protocol client works: the command is `kas3c lsp`, over standard input and
output, for files ending in `.as`.

| Option | What it does |
|---|---|
| `--no-godot` | Don't load Godot's API. |
| `--declarations <file>` | Load one more declaration file. May be repeated. |

## What works

| Feature | Notes |
|---|---|
| Errors as you type | What `kas3c check` reports. While a file has a syntax error, only syntax errors are shown. |
| Go to definition | Locals, parameters, fields, methods, classes, imports, inherited members, and Godot's API. |
| Find references | Across the project's files. |
| Hover | The declaration, and the class or package it belongs to. |
| Outline | The classes and members of a file. |
| Symbol search | The project's own classes, functions and members. |

[Completion and typing assistance](completion.md) covers chained members, types, imports, visible names,
Tab replacement, callable snippets and argument help. Features that depend on client capabilities have
plain-text and ordinary-edit fallbacks.

Not yet: rename and Godot's documentation in hover.

Going to the definition of something in Godot's API opens a read-only copy of the declarations, which the
server writes to the system's temporary folder.
