# `godot/`: Godot's API as AS3

`generate.py` reads Godot's own description of its API and writes two files that the build embeds in
`kas3c`:

- **`godot.as`**: AS3 declarations, which the checker reads like any source file.
- **`godot_names.txt`**: what the lowerer and the extension need that AS3 declarations can't say, such as
  Godot's own names, method hashes, types and defaults.

For how the result looks to someone writing scripts, read
[Using AS3 in Godot](../../docs/godot.md#how-godots-api-is-named).

**Depends on:** Python 3 and nothing else. Godot's API description isn't in this repository.

## Files

| Files | Job |
|---|---|
| `generate.py` | The rules, one for each kind of thing Godot describes |
| `asdoc.py` | Formats reference summaries and links to longer entries for the editor |
| `naming.py` | Godot's names to AS3's: `get_node` to `getNode` |
| `godot.as`, `godot_names.txt` | Generated. Never edit them by hand |
| `runtime_names.py` | Extracts the names used for dynamic signal access and the signal emission method bind from the generated name table |
| `embed.py` | Puts the two files inside `kas3c` |
| `fixture_api.json`, `fixture.as`, `test_*.py` | A small API for the tests |
| `NOTICE.md` | Godot's license, for the names |

## What the generator decides

- **Documentation.** ASDoc comments carry each available class summary or the first paragraph of a
  member description. Constructor help lists the documented forms with their AS3 parameter types.
  References use the AS3 names for properties and constants. Settings keys keep their engine spelling
  so they can be passed to `getSetting`. Inline links retain their destinations. Entries with more text include
  the URL of the full versioned reference. A plain API dump still generates declarations, but provides
  no engine descriptions.
- **Names.** Methods, properties, signals and parameters are lowerCamel, with acronyms in capitals
  (`getAABB`). A name AS3 reserves gets a trailing underscore. Five class names that AS3 already uses get
  a `Godot` prefix, such as `GodotObject`. A getter and a setter become one property.
- **Integers.** A Godot integer is `Int64`. It's an `int` or a `uint` only where Godot's description says
  it's narrower: by a width mark, or by belonging to a type that stores 32-bit integers, such as
  `Vector2i`. There is no table of exceptions for single methods.
- **Only virtual methods can be overridden.** Every other method, property and signal is `final`.
- **`toString()` is AS3's.** Godot's `to_string()` and `_to_string()` aren't declared. The extension maps
  them.
- **A class the engine won't create** is tagged `[Abstract]`, and `new` on it is an error. A script can
  still extend one.
- **Enums are `int`.** A `[Godot(...)]` tag on the member records which enum a parameter or a result is.
- **Defaults.** A default that isn't a number, a Boolean, a string or `null` is written `null`. The real
  default is applied when the call is made.
- **Value types** have one constructor that accepts each of Godot's forms.
- **Operators** are tags on each class: `[Operator(op="+", right="Vector2", returns="Vector2")]`.
- **Singletons** are classes with static members.
- **`GodotObject`'s virtual methods and `free()`** are missing from Godot's description. The generator
  adds them from a list of its own.
- **Left out:** methods that take raw pointers; `typeof` and `isFinite`, which AS3 has; and Godot's own
  `String`, `StringName`, `NodePath` and `Callable` classes, for which a script writes `String` and
  `Function`.

The generator stops on anything it has no rule for: an unknown kind of type, or a name that clashes with
another or with an AS3 built-in.

## The name table

A line of `godot_names.txt` is `<key>=<value>`, sorted by key. The value starts with Godot's own name.

| Key | Value |
|---|---|
| `Node2D` | Godot's name for the class |
| `Node.isInsideTree` | `is_inside_tree method <hash> <result> <parameters>` |
| `Node2D.position` | `position property <type> <getter> <hash> <setter> <hash> <index>` |
| `Node.ready`, `Vector2.x` | `ready signal`, and `x field float` |
| `Vector2.ZERO`, `Input.singleton` | The constant's name, and `@singleton` |
| `@operator.OP_ADD` | `+ 6`: the operator and Godot's number for it |
| `@global.lerpf` | `lerpf <hash> <result> <parameters>` |
| `@default.Color.clamp.0` | Godot's text for a default that AS3 can't write |

A `-` stands for a part that's missing. Types are Godot's, as its description writes them, such as
`enum::Node.ProcessMode`. A key never contains `=`.

## Rules

- **A change to what AS3 sees of Godot is a rule in the generator.** Write a test in `test_generate.py`
  first, then regenerate both files and commit them with the change.
- **Hand-kept lists are checked.** An entry that matches nothing in Godot's description fails a test.
- **The description is a downloaded file.** Keep it out of the repository's source folders, and don't
  commit it.

## Regenerate for a Godot version

1. Write Godot's API description **with documentation**:

   ```sh
   godot --headless --dump-extension-api-with-docs
   ```

2. Run the generator on it:

   ```sh
   python3 compiler/godot/generate.py extension_api.json compiler/godot/godot.as compiler/godot/godot_names.txt
   ```

## Tests

The `godot` group:

```sh
python3 -m unittest discover -s compiler/godot -p "test_*.py"
```

The tests of Godot's whole API need the description. Set `KAS3_GODOT_API` to its path. Without it those
tests are skipped and say so.
