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

> **Windows is not supported yet.** The server runs on macOS and Linux. On Windows it does not read the
> editor's messages correctly and does not recognise which files belong to the project.

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

## Rider and other JetBrains IDEs

The [Kumikae JetBrains plugin](jetbrains/) connects `kas3c lsp` through the IDE's native LSP client.
It provides AS3 highlighting, completion, argument help, navigation and local bracket/quote assistance,
as well as project/run tools and the Simulator panel. Install the plugin ZIP built from the same
compiler revision; it bundles `kas3c` when a compiler build is available. See the plugin README for the
supported Rider build and build commands.

The native plugin does not need LSP4IJ. Other JetBrains products/builds require separate verification;
Android Studio and open-source IntelliJ builds do not provide its required JetBrains LSP module.

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
