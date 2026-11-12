# Editors

`kas3c lsp` is a language server: an editor starts it, and it reports the compiler's errors as you type and
answers go to definition, find references, hover, outline and symbol search. It is the same program as
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

> **Not yet tried in an editor.** The server is tested by scripted sessions that send what an editor sends.
> The two set-ups below are written from each editor's documentation and have not been run by hand.

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

JetBrains IDEs take a language server through a plugin. Until this project has its own plugin, the
[LSP4IJ](https://github.com/redhat-developer/lsp4ij) plugin connects any language server without one:

1. **Settings > Plugins > Marketplace**, install **LSP4IJ**.
2. **Settings > Languages & Frameworks > Language Servers**, press **+**.
3. **Name:** `Kumikae AS3`. **Command:** the full path of `kas3c`, then `lsp`.
4. On the **Mappings** tab, under **File name patterns**, add `*.as`.

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

Not yet: completion, signature help, rename, Godot's documentation in hover.

Going to the definition of something in Godot's API opens a read-only copy of the declarations, which the
server writes to the system's temporary folder.
