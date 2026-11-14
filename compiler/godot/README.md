# `godot/`: Godot's API as AS3

`generate.py` reads Godot's own description of its API and writes two files the compiler ships with:
`godot.as` (declarations the checker reads like any source) and `godot_names.txt` (what the lowerer and the
extension need that AS3 declarations cannot say: Godot's own names, hashes, defaults).

| Files | Job |
|---|---|
| `generate.py` | The rules, one per kind of thing Godot describes |
| `naming.py` | Godot's names to AS3's (`get_node` to `getNode`), by the naming design (`docs/superpowers/specs/2026-11-12-godot-naming-design.md`) |
| `godot.as`, `godot_names.txt` | **Generated. Never edited by hand** |
| `embed.py` | Puts the two files inside `kas3c` |
| `fixture_api.json`, `fixture.as`, `test_*.py` | A small API for the tests |
| `NOTICE.md` | Godot's licence, for the names |

## Rules

- **A change to what AS3 sees of Godot is a rule in the generator,** with a test in `test_generate.py`
  first, then both files regenerated and committed with it:

```sh
python3 compiler/godot/generate.py spike/api/extension_api.json compiler/godot/godot.as compiler/godot/godot_names.txt
```

- **A Godot integer is `Int64` unless Godot's description says it is narrower.** No per-method table.
- **Hand-kept lists are checked:** an entry that matches nothing in Godot's description fails a test.
- Python reads the description with `-I`: it is a downloaded file. `spike/api/` is never committed.
- No text in a name-table line may contain a dot where the interpreter splits on the last one.

Tests: the `godot` group (`python3 -m unittest discover -s compiler/godot -p "test_*.py"`).
