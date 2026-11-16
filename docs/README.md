# `docs/`

| Where | Holds |
|---|---|
| [roadmap.md](roadmap.md) | The stages, what is done, what is next |
| [backlog.md](backlog.md) | Old findings still owed, being fixed until the page is empty. Nothing is added to it: what is found is fixed at once (the owner's rule, 2026-11-15) |
| [cpp-guidelines.md](cpp-guidelines.md) | How the C++ is written; `compiler/tools/check_style.py` enforces what a program can check |
| `superpowers/specs/` | A design, approved by the owner before the work starts: `YYYY-MM-DD-<topic>-design.md` |
| `superpowers/plans/` | The plan that carries a design out, task by task, with its review focus |
| `results/` | What was measured or run, on what, on which date. Only what was observed: a thing not run is listed as not run |
| `diagrams/` | `build.py` draws the `.svg` files; edit the script, not the picture |

`superpowers/` is ignored by git: designs and plans are working papers, kept where they are written and never
committed. What a design decided that others must know goes into the roadmap or a README.

## Rules

- Plain words, short sentences. A document says what is true now; history is in git.
- A results file never claims more than was run. No device, team or personal identifier in one.
- Nothing about a games console's SDK, tools or results.
- A stage is not done until the roadmap and the README of what changed say so, and every finding of its
  review is fixed.
