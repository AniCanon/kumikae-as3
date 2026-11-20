# Editor support

This page shows how to set up an editor for ActionScript 3. Editing works through a language server,
`kas3c lsp`, which is the compiler itself. Your editor shows the same errors as `kas3c check` and as Godot.

| Editor | What you get | Set it up |
|---|---|---|
| Rider, IntelliJ IDEA | Language support, a project wizard, run configurations, a test runner, settings and scene editors, and a Game Devices panel | [Rider and IntelliJ IDEA](#rider-and-intellij-idea) |
| Claude Code | Language support, game builds, launches and gameplay tests | [Claude Code](claude/README.md) |
| VS Code | Language support and syntax coloring | [VS Code](#vs-code) |
| Any editor with a Language Server Protocol client | Language support | [Other editors](#other-editors) |

## Before you start

Build the compiler. The language server is the same program.

```sh
cmake -S compiler -B compiler/build
cmake --build compiler/build -j
```

The server is then `compiler/build/kas3c`.

In your editor, open the folder that holds `project.godot`, or any folder inside it. Every `.as` file in
the project is part of the program.

## Rider and IntelliJ IDEA

1. Build the plugin. You need JDK 25 and Rider 2026.2 installed. See
   [the plugin's README](jetbrains/README.md#build-the-plugin) for the details.

   ```sh
   cd editors/jetbrains
   JAVA_HOME=/path/to/jdk-25 ./gradlew buildPlugin
   ```

2. In the IDE, open **Settings > Plugins**, click the gear icon, and choose **Install Plugin from Disk**.
3. Select the zip file in `editors/jetbrains/build/distributions/`.
4. Restart the IDE.

The plugin bundles `kas3c` when the compiler was built before the plugin, so there's nothing else to
configure.

In IntelliJ IDEA, also install the **LSP4IJ** plugin from **Settings > Plugins**. Rider doesn't need it.

The [plugin's README](jetbrains/README.md) describes everything the plugin does.

## VS Code

The client is in [`vscode/`](vscode/README.md). It isn't published to the marketplace, so you run it from
this repository.

1. Install the client's one dependency:

   ```sh
   cd editors/vscode
   npm install
   ```

2. Open the `editors/vscode` folder in VS Code and choose **Run > Start Debugging**. A second window opens
   with the extension loaded.
3. In that window, open your project.
4. Set **Kumikae AS3: Server Path** (`kas3.serverPath`) to the full path of `kas3c`.

| Setting | What it does | Default |
|---|---|---|
| `kas3.serverPath` | The `kas3c` program: a full path, or a name found on the `PATH` | `kas3c` |
| `kas3.godot` | Loads Godot's API | On |
| `kas3.declarations` | More [declaration files](../docs/language.md#declaration-files) to load | None |

## Other editors

Point your editor's Language Server Protocol client at this command, for files that end in `.as`:

```sh
kas3c lsp
```

The server talks over standard input and output.

| Option | What it does |
|---|---|
| `--no-godot` | Doesn't load Godot's API |
| `--declarations <file>` | Loads one more declaration file. You can repeat it |
| `--stdio` | Accepted and ignored, because some clients add it |

## What works

| Feature | Notes |
|---|---|
| Errors as you type | What `kas3c check` reports. A file with a syntax error shows only that error. Files that use nothing from it keep their other errors |
| Completion | Members after a dot, types, imports, names in reach, override stubs, and language blocks such as `try` and `for`. See [Completion and typing help](completion.md) |
| Signature help | The declaration being called and the active parameter |
| Go to definition | Locals, parameters, fields, methods, classes, imports, inherited members, and Godot's API |
| Find references | Across the project's files |
| Hover | The declaration, and the class or package it belongs to |
| Outline | The classes and members of a file |
| Symbol search | The project's own definitions, then Godot's classes and the built-in library's |

When you go to the definition of something in Godot's API, the editor opens a read-only copy of the
declarations, which the server writes to the system's temporary folder.

## What doesn't work yet

- Rename and code actions.
- Godot's documentation in hover.
- A debugger. See the [roadmap](../docs/roadmap.md).
- Windows and Linux. The server and the plugin are written to run there, and neither has been built or
  run on them.
