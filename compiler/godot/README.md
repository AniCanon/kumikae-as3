# `godot/`: Godot's API as AS3

`generate.py` reads Godot's own description of its API and writes two files the compiler ships with:
`godot.as` (declarations the checker reads like any source) and `godot_names.txt` (what the lowerer and the
extension need that AS3 declarations cannot say: Godot's own names, hashes, types, defaults).

A line of `godot_names.txt` is `<key>=<value>`, sorted by key; the value starts with Godot's own name:

| Key | Value |
|---|---|
| `Node2D` | Godot's name for the class |
| `Node.isInsideTree` | `is_inside_tree method <hash> <result> <parameters>` (`-` for none; `varargs` after them for any number more; hash `-` for one the engine keeps to itself) |
| `Node2D.position` | `position property <type> <getter> <hash> <setter> <hash> <index>` (`-` where there is none) |
| `Node.ready`, `Vector2.x` | `ready signal`; `x field float` |
| `Vector2.ZERO`, `Input.singleton` | The constant's name; `@singleton` |
| `@operator.OP_ADD` | `+ 6`: the description's name for the operator and Godot's number for it (the name would end the key) |
| `@global.lerpf` | `lerpf <hash> <result> <parameters> [varargs]` |
| `@default.Color.clamp.0` | Godot's text for a default AS3 can't write |

Types are Godot's, as its description writes them (`enum::Node.ProcessMode`, `typedarray::Node`).

| Files | Job |
|---|---|
| `generate.py` | The rules, one per kind of thing Godot describes |
| `naming.py` | Godot's names to AS3's (`get_node` to `getNode`) |
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
- A key never holds `=`. The interpreter splits a static call's text at its first dot: a class name has none.

Tests: the `godot` group (`python3 -m unittest discover -s compiler/godot -p "test_*.py"`).
