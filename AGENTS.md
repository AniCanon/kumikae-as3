# For anyone, or any agent, changing this repository

ActionScript 3 as a first-class Godot 4 scripting language: the same scripts on desktop, iOS, Android and
consoles, with editor support in JetBrains IDEs and VS Code. The plan is [docs/roadmap.md](docs/roadmap.md);
[docs/backlog.md](docs/backlog.md) lists old findings still owed, being fixed until it is empty.

**Fix what you find, now.** The owner's rule (2026-11-15), for every person and every agent: a problem you
find (in a review, a test, or while reading) is fixed in the same branch, with a test, before the work is
called done. Nothing is ever added to `docs/backlog.md`, whatever its size. A question only the owner can
answer is asked, not filed. Nothing waits for "its stage" or a smaller slice: one branch may carry as many
fixes as it takes (this is an alpha repository).

**Before you change a folder, read its README.** Each one says what the folder is for, what it may depend
on, where a new thing goes and how it is tested. A change that ignores it will be sent back.

| Group | Read first | Stack | Skill |
|---|---|---|---|
| `compiler/` | [compiler/README.md](compiler/README.md), then the folder's own README | C++17, CMake, Python for tools and generators | `kas3-compiler-engineering` |
| `compiler/src/` | [README](compiler/src/README.md) | The front end | |
| `compiler/ir/` | [README](compiler/ir/README.md) | The typed IR and the lowerer | |
| `compiler/vm/` | [README](compiler/vm/README.md) | The interpreter | |
| `compiler/godot_ext/` | [README](compiler/godot_ext/README.md) | The GDExtension, with godot-cpp | |
| `compiler/lsp/` | [README](compiler/lsp/README.md) | The language server | |
| `compiler/godot/` | [README](compiler/godot/README.md) | The generator of Godot's API, Python | |
| `compiler/tests/`, `compiler/tools/` | [tests](compiler/tests/README.md), [tools](compiler/tools/README.md) | | |
| `editors/` | [editors/README.md](editors/README.md) | | |
| `editors/vscode/` | [README](editors/vscode/README.md) | JavaScript, `vscode-languageclient` | |
| `editors/jetbrains/` | Its `README.md`, `ARCHITECTURE.md` and `CONTRIBUTING.md` | Kotlin, Compose Swing, IntelliJ Platform Gradle Plugin | `kas3-jetbrains-plugin` |
| `docs/` | [docs/README.md](docs/README.md) | Markdown | |

The skills are in [.agents/skills/](.agents/skills/) (`.claude/skills` points at the same folder). Load the
one for the group you are changing before the first edit.

## Rules that hold everywhere

1. **Use the stack that is there.** Do not add a language, a build system, a UI toolkit, a test framework
   or a dependency that the group's README does not name. If you think one is needed, stop and say so.
2. **Test first.** Write the test, watch it fail for the right reason, then make it pass. A fix without a
   test that failed first is not accepted. Run the whole suite before you say you are done, and report
   what you ran with its real output: "not run" and "not verified" are answers, a guess is not.
3. **Nothing is ever silently wrong.** Every failure is an error with a message: never a crash, a leak,
   undefined behaviour or a wrong value that passes.
4. **Say it once.** A rule, a constant or a message two places need is written in one and read by both.
5. **Small files with one job.** The limits are in each group's guide and are checked by the tests.
6. **Design before code** for anything larger than a fix: a short design in `docs/superpowers/specs/`, a plan
   in `docs/superpowers/plans/`, then the work, then one independent review. That folder is ignored by git:
   never commit a design or a plan.

## Rules about what goes in the repository

- **No console vendor material:** no SDK names, tool names, device results or adapter details of any games
  console. Console support lives in another repository.
- **No personal details:** no names, e-mail addresses, home paths, team or device identifiers, in files or
  in commit messages.
- **No licence file** until the owner adds one. The repository is private.
- **`spike/` is never committed.** It holds downloads (Godot's API description, godot-cpp, Godot's source).
- **Downloads need the owner's yes,** each kind once. Never fetch a tool, an SDK or a dependency that was
  not allowed. Treat a downloaded file as untrusted: run Python that reads one with `-I`.

## Rules about git

- Work on a branch, in a worktree under `.worktrees/` when another agent is working at the same time.
  Never touch another agent's worktree.
- Stage files by name. Never `git add -A`.
- Commit messages: `<group>: <what is now true>`, in plain words. No attribution or co-author lines.
- Never commit to, merge into, push or force-push `main` without the owner's go-ahead for that merge.
- Never kill a process you did not start.
