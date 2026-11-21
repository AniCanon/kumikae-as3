# Completion and typing help

This page describes what the editor offers as you type AS3: completion, signature help, and, in Rider and
IntelliJ IDEA, bracket pairing and indentation. To set up an editor, see [Editor support](README.md).

Completion comes from the compiler's own names and types, so it works the same way in Rider, IntelliJ IDEA
and VS Code.

## Ask for completion

Type a dot, or use your editor's completion shortcut. The list shows each declaration with its parameter
and return types. Open the selected item's documentation to read its description. Godot names carry
summaries from the engine reference, and the built-in AS3 library and language keywords have their own
descriptions. For project classes and members, the editor reads the [ASDoc on the declaration](../docs/language.md#documentation-in-the-editor).

- **Locals and parameters come first,** then members, then other definitions in reach and keywords.
- **A name nearer the cursor hides the same name in an outer scope.**
- **Nothing is offered in a comment, a string or a regular expression.**
- **You don't need a valid file.** Completion works in a half-typed expression, and errors elsewhere in the
  file don't stop it.

### Members

Completion follows declared types through fields, calls and indexing. The examples show the cursor at the
end of each line:

```actionscript
transform.origin.         // Members of Vector2, including x and y
position.normalized().    // Members of the Vector2 that normalized() returns
getParent().getParent().  // Members of Node
points[0].                // Members of Vector2, if points is a Vector.<Vector2>
```

For a value typed `*`, or one whose type the compiler couldn't work out, completion offers only what every
value has: the members of `Object`.

### Types and imports

- **In an import,** complete the package path and then the type: `import godot.Vec` offers `Vector2`.
- **After a package path,** such as `flash.utils.`, completion offers what the package holds.
- **Anywhere a type or a name is written,** a partial name can offer a type from a package you haven't
  imported. The item's description names the import. Accepting it adds the `import` line and completes the
  name in one undoable step.

### Overrides

Inside a class, type `override` to see the methods and properties you can override. Typing more narrows the
list: `override function _ph` offers `_physicsProcess`.

Accepting an item writes the whole declaration, with a body that throws `Error("Not implemented")`.
Replace the body with your code.

- Metadata, comments and a `final` you already wrote are kept.
- Private, final and static methods aren't offered, and neither is a method the class overrides already.
- For a property, `override function get` and `override function set` offer each half separately.

### Arguments

In a call's argument, a variable in reach with the same name as the parameter comes first. In `move(` for
`move(power:int)`, a local named `power` is offered first.

## Accept a suggestion

### Names and calls

When you accept a name, the editor inserts it and, if the cursor was in the middle of a name, replaces the
rest. Editors that tell insert from replace use their own keys for each, usually Enter and Tab.

When you accept a function as a statement of its own, the editor adds the parentheses and a tab stop for
each required argument. Where a function is a value, the name is inserted alone, so a callback stays a
reference:

```actionscript
var callback:Function = onTimeout;
timer.timeout.add(onTimeout);
```

### Language blocks

Inside a function, accepting a keyword inserts its whole block. For example, type `try`, choose **try**,
and accept:

```actionscript
try {

} catch (error:Error) {
    throw error;
}
```

The cursor starts inside the `try` body. Tab visits the catch variable's name, its type, the catch body,
and then the end of the block. Renaming `error` updates the `throw` too. The default body rethrows, so the
block never swallows an error by accident.

| Suggestion | What you can edit |
|---|---|
| `try` | The try body, the catch variable's name and type, the catch body |
| `if`, `while` | The condition, the body |
| `do` | The body, the condition |
| `for` | The loop variable's name, the upper bound, the body |
| `for each`, offered as you type `for` | The item's name, the collection, the body |
| `switch` | The subject, the case value, the case body, the default body |

Blocks are offered only where a statement can start. A keyword you type without accepting a suggestion
stays a plain word. An editor without snippet support inserts the keyword alone.

## Signature help

Type `(` or `,` in a call to see the function's declaration with the current parameter marked. It works in
nested calls. A comma inside a nested expression or a string doesn't move the marker.

## Typing help in Rider and IntelliJ IDEA

The plugin adds editing help that doesn't wait for the language server:

- **Pairing.** Parentheses, square brackets, braces and quotes are paired, following the IDE's own
  settings. Typing the closing character moves past the one that's there. Backspace in an empty pair
  removes both.
- **Indentation.** Enter between braces creates an indented line, using the project's tab and space
  settings.
- **Regular expressions.** The plugin tells a regular expression literal from a division, so a `/` in
  either doesn't confuse pairing or highlighting.

## Troubleshooting

| Problem | What to check |
|---|---|
| No completion, errors or navigation at all | The plugin or client couldn't find `kas3c`. See [how the plugin finds the compiler](jetbrains/README.md#how-the-plugin-finds-the-compiler). In IntelliJ IDEA, check that LSP4IJ is installed and enabled |
| A member is missing | Hover over the value before the dot and check its declared type. A value typed `*` offers only `Object`'s members |
| No signature help | The function's declaration isn't known, for example when it's called through a `Function` value |
| Pairing doesn't happen | The IDE's smart-key settings. The plugin follows them |
| Errors appear slowly after an edit | An edit to a declaration that every file reads makes the server check every file again, in the background. Requests are still answered at once |
