# Contributing

This page is for anyone changing the code, whether a person or a coding agent.

## Before you start

Read the README of the folder you're changing. Each one says what the folder is for, what it may depend on,
where a new thing goes, and how it's tested.

| Part | Read first | Stack |
|---|---|---|
| `compiler/` | [compiler/README.md](compiler/README.md), then the folder's own README, and the [C++ guidelines](docs/cpp-guidelines.md) | C++17, CMake, Python for tools and generators |
| `editors/jetbrains/` | Its [README](editors/jetbrains/README.md), [ARCHITECTURE](editors/jetbrains/ARCHITECTURE.md) and [CONTRIBUTING](editors/jetbrains/CONTRIBUTING.md) | Kotlin, Compose, IntelliJ Platform Gradle Plugin |
| `editors/vscode/` | Its [README](editors/vscode/README.md) | JavaScript |
| `testing/` | Its [README](testing/README.md) | AS3 and Python |
| `docs/` | [docs/README.md](docs/README.md) | Markdown |

## How a change is made

1. **Write the test first.** Watch it fail for the right reason, then make it pass. A fix without a test
   that failed first isn't accepted.
2. **Keep the change small and of one kind.** A move, a rename and a rewrite are separate commits.
3. **Fix what you find.** A problem you come across while working is fixed in the same branch, with a test.
   The project keeps no list of known bugs to fix later.
4. **Update the docs in the same pull request.** If the change alters what the project does, the
   [roadmap](docs/roadmap.md) and the README of what changed say so.
5. **Run the applicable checks** below, read their output, and report what you ran. "Not run" is an acceptable answer;
   a guess isn't.

## Checks

Documentation-only changes, including license text and attribution notices, can skip all build and test
steps below, both before opening a pull request and before merging it.

For other changes, run the following from the repository root before opening a pull request:

```bash
cmake --build compiler/build -j && ctest --test-dir compiler/build
```

If you changed the Godot extension, also build it and run the tests inside Godot. Both need a godot-cpp
checkout for Godot 4.7; see [compiler/tests/README.md](compiler/tests/README.md).

```bash
cmake --build compiler/build-godot -j
python3 compiler/tests/run_godot.py compiler/tests compiler/build/kas3c
```

Before a merge, run the suite under the sanitizers:

```bash
cmake -S compiler -B compiler/build-san -DKAS3_SANITIZE=ON
cmake --build compiler/build-san -j && ctest --test-dir compiler/build-san
```

A change made for speed comes with numbers, measured before and after in alternating runs. See
[Performance](docs/performance.md).

## Rules that hold everywhere

- **Nothing is silently wrong.** Every failure is an error with a message. No crash, leak, undefined
  behavior or wrong value that passes.
- **Use the stack that's there.** Don't add a language, a build system, a framework or a dependency that
  the folder's README doesn't name. If you think one is needed, open an issue first.
- **Say it once.** A rule, a constant or a message that two places need is written in one and read by both.
- **Small files with one job.** The size limits are in each part's guide, and the tests check them.

## What doesn't belong in the repository

- **Console vendor material.** No SDK names, tool names, device results or adapter details of any games
  console. Console support lives in another repository.
- **Personal details.** No names, email addresses, home paths, or team and device identifiers, in files or
  in commit messages.
- **Downloads and build output.** `spike/` holds local downloads such as godot-cpp and is ignored by git.

## Git

- Work on a branch. Stage files by name, not with `git add -A`.
- Write commit messages as `<part>: <what is now true>`, in plain words.
- `main` holds work that passes the checks. Changes reach it through pull requests.
