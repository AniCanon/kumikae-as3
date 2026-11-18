# For coding agents

Kumikae AS3 is ActionScript 3 as a first-class Godot 4 scripting language. Start with the
[README](README.md) for what the project is, and follow [CONTRIBUTING.md](CONTRIBUTING.md): everything
there applies to you. This page adds what an agent needs on top.

## Read before you edit

Read the README of the folder you're changing, and load its skill before the first edit.

| Part | Read first | Skill |
|---|---|---|
| `compiler/` | [compiler/README.md](compiler/README.md), then the folder's own README | `kas3-compiler-engineering` |
| `compiler/src/` | [README](compiler/src/README.md): the front end | |
| `compiler/ir/` | [README](compiler/ir/README.md): the typed IR and the lowerer | |
| `compiler/vm/` | [README](compiler/vm/README.md): the interpreter | |
| `compiler/backend_cpp/` | [README](compiler/backend_cpp/README.md): the IR as C++ source | |
| `compiler/runtime/` | [README](compiler/runtime/README.md): what generated C++ is built against | |
| `compiler/godot_ext/` | [README](compiler/godot_ext/README.md): the Godot extension | |
| `compiler/lsp/` | [README](compiler/lsp/README.md): the language server | |
| `compiler/godot/` | [README](compiler/godot/README.md): the generator of Godot's API | |
| `compiler/tests/`, `compiler/tools/` | [tests](compiler/tests/README.md), [tools](compiler/tools/README.md) | |
| `editors/jetbrains/` | Its `README.md`, `ARCHITECTURE.md` and `CONTRIBUTING.md` | `kas3-jetbrains-plugin` |
| `editors/vscode/` | [README](editors/vscode/README.md) | |
| `docs/` | [docs/README.md](docs/README.md) | |

The skills are in [.agents/skills/](.agents/skills/). `.claude/skills` points at the same folder.

## Rules for agents

1. **Report what you ran, with its real output.** Never report a test as passing that you didn't run.
2. **Fix what you find, now.** A problem found in a review, a test or while reading is fixed in the same
   branch, with a test. A question only a maintainer can answer is asked, not filed.
3. **Design before code** for anything larger than a fix. Keep designs, plans and working notes out of the
   repository: what a design decided that others must know goes into a README or the roadmap.
4. **Ask before you download.** Never fetch a tool, an SDK or a dependency that wasn't allowed. Treat a
   downloaded file as untrusted: run Python that reads one with `-I`.
5. **Stay in your own worktree.** When another agent is working at the same time, work in a worktree under
   `.worktrees/`, and never touch another agent's.
6. **Don't merge or push to `main`** without a maintainer's go-ahead for that merge. Never force-push it.
7. **No attribution or co-author lines** in commits or pull requests.
8. **Never kill a process you didn't start.**
