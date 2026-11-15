# Completion and typing assistance

Kumikae uses the compiler's names and types for completion. Rider's native LSP client and the VS Code
client use the same `kas3c lsp` implementation, each in its own process. Local punctuation and indentation in Rider use the IDE's
editor handlers; typing a bracket does not wait for the language server.

## Using completion

Invoke **Code > Code Completion > Basic** or use the shortcut assigned by your keymap. Typing a dot
also requests members of the value before it. The popup shows declarations, including parameter and
return types. Locals and parameters come before members, then reachable definitions and keywords.
A name nearer the caret's scope hides the same name in an outer scope.

Completion follows declared types through fields, calls and typed indexing:

```actionscript
transform.origin.         // Vector2 members, including x and y
position.normalized().    // Members of the Vector2 returned by normalized
getParent().getParent().  // Members of Node
points[0].                // Vector2 members if points is Vector.<Vector2>
```

For a `*` receiver, or one whose type could not be worked out, only what every value has is offered:
`Object`'s members (`toString`, `hasOwnProperty` and the rest). After a package's path (`flash.utils.`,
`godot.`) completion offers what that package holds and the packages inside it; where a type is written
(`var d:flash.utils.`), its types only. A name in reach comes before a package of the same name. Nothing
is offered after a name that means nothing in the file. A partial expression can still be
completed; errors elsewhere do not require saving a valid file first. Comments, strings and regular
expressions do not offer code completion. Parameters, function-wide local variables, outer captures,
named function expressions, catch variables and function-local imports respect their compiler scope.

In a class, after `override` (with or without the access word and `function`), completion offers the
methods and properties of the base classes that may be overridden: not private, static or final ones,
not built-in ones (Flash takes those without `override`), and not one this class overrides already.
Taking one writes the rest of its declaration: the access word and `function` if they are not there yet,
the parameters with their defaults, the result, and a body that calls the base class's version (`{}` for
a method that gives back nothing). A type the file cannot name by itself is written in full, as in
`lib.Thing`. A rest parameter is not passed on to the base class's version.

In an import, complete the package path and type, for example `import godot.Vec`. Completion does not
silently add imports to the file. Types must already be visible to appear as unqualified names.

## Accepting a suggestion

The server supplies the identifier's insertion and replacement ranges, including the suffix when the
caret is in the middle of a name. Clients that support separate ranges can use their normal Enter
insertion and Tab replacement behavior. Clients with ordinary text edits replace the entire identifier.
The exact keys are controlled by the editor's keymap.

For a callable used as a standalone expression statement, a client with snippet support receives
parentheses and tab stops for required arguments. Optional and rest parameters remain visible in the
declaration and argument help. An existing argument list is reused. In an assignment, return or
argument position, the name is inserted conservatively so a callback stays a function reference:

```actionscript
var callback:Function = onTimeout;
timer.timeout.add(onTimeout);
```

In a call's argument, a variable, parameter or member in reach whose name is the parameter's name there
comes first and is preselected: in `move(|)` for `move(power:int)`, a local `power` is offered first. No
name is made up when none is in reach, since AS3 has no named arguments.

Typing `(` or `,` requests argument help. It shows the callable's declaration and active parameter,
including inside nested calls. Commas inside nested expressions, strings, comments and regular
expressions do not advance the outer call's active parameter.

## Rider typing assistance

Parentheses, square brackets, braces and single/double quotes use the IDE's pairing settings. A closing
partner can be typed to move past the existing one. Backspace in an empty bracket pair removes the pair.
Enter inside braces creates an indented line, using the project's tab/space settings. Code punctuation
inside comments and strings is not treated as a new code block. Regex literals are protected in
expression positions and after control conditions. The local lexer cannot distinguish every regex
from division after a closing brace; that context remains a limitation. The compiler handles regex
contexts independently for completion and argument help.

Highlighting still comes from the shared TextMate grammar. A small adapter supplies AS3 token boundaries
to the native editing handlers while preserving TextMate's color attributes, including string escapes.

## Performance and ownership

The parser records non-code text intervals once; editor queries search those intervals instead of
scanning the file from its beginning on every request. The resolver's scopes belong to the current
compilation. Completion caches declaration descriptions and a compact list of relevant syntax nodes
within that same compilation. An edit releases those caches before rechecking; no old syntax pointers
or history of prefix results survives the generation.

Clients advertising LSP completion-list edit defaults receive the shared range once. Older clients
receive a range with each item. Snippets and separate insert/replace edits are also capability-gated.
Every result describes the current document known to the server.

The server still checks names and types across the project after an edit, reusing unchanged parses.
This change does not introduce concurrent access to mutable syntax trees or a second type checker.
See the [measured results](../docs/results/2026-11-completion.md) for latency, memory, commands and limits.

## Troubleshooting and limits

- If all language features are absent, check that the plugin can find `kas3c`. The JetBrains plugin
  uses a configured compiler first, then a build in the project or its ancestors, then its bundled
  compiler. Rebuilding an unrelated checkout does not update an installed plugin's binary.
- If pairing is disabled, check the IDE's editor smart-key settings. The plugin honors those settings.
- If a member is missing, hover the receiver and check its declared type and visibility. A dynamic value
  offers only `Object`'s members until it has a known type.
- For a sandbox trace, use `./gradlew runIde -PlspTrace=<existing-folder>` from `editors/jetbrains`.
  Trace files contain source text; keep them outside the repository and do not enable them for timings.
- Automatic imports and whole-project incremental type checking remain separate work. The server has not
  yet been tried on Windows. Unknown callable signatures cannot show argument help.
- The plugin is pinned to the Rider build listed in its README. Android Studio and IntelliJ open-source
  builds do not provide the JetBrains LSP module this plugin requires; they are not supported targets.

The [IntelliJ LSP documentation](https://plugins.jetbrains.com/docs/intellij/language-server-protocol.html)
describes the supported products. The [editor extension documentation](https://plugins.jetbrains.com/docs/intellij/additional-minor-features.html)
and [threading guidance](https://plugins.jetbrains.com/docs/intellij/threading-model.html) describe the
platform facilities used here. Validation distinguishes server tests, sandbox editor checks and human
acceptance in the results document.
