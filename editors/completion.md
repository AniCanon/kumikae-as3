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

There are no guessed members for an unknown or `*` receiver. A partial expression can still be
completed; errors elsewhere do not require saving a valid file first. Comments, strings and regular
expressions do not offer code completion. Parameters, function-wide local variables, outer captures,
named function expressions, catch variables and function-local imports respect their compiler scope.

In an import, complete the package path and type, for example `import godot.Vec`. Outside imports,
a partial type name can offer an accessible type from another package. Its description names the import.
Accepting `Vector2` for `Vec` adds `import godot.Vector2;` and replaces the identifier in one undoable action.
An existing visible name takes precedence; duplicate short names keep their qualified descriptions.
An empty-prefix lookup is refreshed as a prefix is typed, so imports do not require reopening the popup.

Inside a derived class, `override function _ph` offers inherited methods such as `_physicsProcess`.
Acceptance adds the signature and a body that throws `Error("Not implemented")`; replace that body with
your implementation. With snippet support, the caret lands immediately before `throw`; generated braces
are ordinary source characters. Metadata, comments and an existing `final` modifier are preserved. Stubs are offered
only while the method name is unfinished, before a parameter list. Private, final and static methods,
inaccessible signature types and non-public member defaults are excluded. An explicitly written
visibility must match the inherited method. Public default constants work when written as `VALUE`,
`Base.VALUE` or `package.name.Base.VALUE`; generated references retain their meaning in the derived class.
Getters/setters use their corresponding accessor prefix. Overriding one does not hide the other, and
`final` applies to the selected accessor. Completion also works immediately after `override function `,
`override function get ` or `override function set `, before any name character is typed. A member on
the following line, including its modifiers and metadata, remains intact.

## Accepting a suggestion

### AS3 language blocks

Inside a function, accept a keyword suggestion to insert its block. For example, type `tr` or `try`,
select **try**, and press Tab:

```actionscript
try {

} catch (error:Error) {
    throw error;
}
```

The first caret is inside the try body. Tab then visits the catch name, its type, the handler body,
and finally the position after the block. Renaming `error` updates the rethrow too. Replace the rethrow
when you implement handling; the default does not swallow the error. The catch type is editable,
including `*` when you intend to catch values other than `Error` objects.

| Suggestion | Editable parts |
|---|---|
| `try` | Try body, catch name/type, catch body |
| `if`, `while` | Condition, body |
| `do` | Body, condition |
| `for` | Linked loop-variable name, upper bound, body |
| `for each` (offered while typing `for`) | Item name, collection, body |
| `switch` | Subject, case value, case body, default body |

Names such as `condition`, `count`, `items` and `value` are editable placeholders: replace them with
your expressions. Tab visits placeholders in order and then leaves the template. Accepting a block is
one undoable insertion. Later placeholder edits and navigation use the IDE's normal Undo history;
Rider can first undo a navigation step without changing the source text.

Expansion requires a statement position and does not replace an existing parenthesis or block.
Expression positions, imports, types, class/file scope, comments, strings and regex literals do not
expand language blocks. Empty gaps after `{`, `;` or `}` offer blocks directly; after a statement without
a semicolon, type a keyword prefix first. Other keywords remain words, and clients without snippet
support receive words too. Typing alone never expands a block: accept the completion suggestion.

### Names and calls

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

Typing `(` or `,` requests argument help. It shows the callable's declaration and active parameter,
including inside nested calls. Commas inside nested expressions, strings, comments and regular
expressions do not advance the outer call's active parameter.

## Rider typing assistance

Parentheses, square brackets, braces and single/double quotes use the IDE's pairing settings. A closing
partner can be typed to move past the existing one. Backspace in an empty bracket pair removes the pair.
Enter inside braces creates an indented line, using the project's tab/space settings. Code punctuation
inside comments and strings is not treated as a new code block. The local lexer tracks expression and statement braces, control conditions, function declarations and
expressions, ternary expressions and labels to distinguish regex literals from division. Its restart
state carries common nesting directly; deeper nesting reconstructs context from the text prefix without
retaining a document history. The compiler supplies its own parsed regions for completion and argument help.

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

After a clean check, edits confined to method/function bodies reuse stable declaration symbols and
recheck the changed units. Sources are immutable shared snapshots; body symbols and scopes belong to
the changed unit and are replaced together. Declaration changes, syntax errors, interfaces, accessors,
function-local imports and recovery from an initially invalid program use a full check. Type-table growth
is bounded by periodically rebuilding the semantic environment. This is a conservative fast path, not
a general dependency graph for incremental checking.

Document changes with old or duplicate versions are ignored with a log message. Diagnostic versions are
sent when the client advertises support, including empty diagnostic lists for each newly checked clean
version. If typing overtakes an earlier clear, the next clean version clears stale errors again.
Rider receives diagnostic versions and ranges atomically with respect to document writes, so a reply
cannot cache pre-edit offsets while a deletion is still in progress.
A bounded lookahead batch cancels queued requests or rejects
requests made obsolete by a newer document change before checking. Partial frames do not delay an
already complete request. A check already running remains synchronous and cannot be interrupted.
See the [follow-up results](../docs/results/2026-11-completion-followup.md) for latency, memory and validation.

## Troubleshooting and limits

- If all language features are absent, check that the plugin can find `kas3c`. The JetBrains plugin
  uses a configured compiler first, then a build in the project or its ancestors, then its bundled
  compiler. Rebuilding an unrelated checkout does not update an installed plugin's binary.
- If pairing is disabled, check the IDE's editor smart-key settings. The plugin honors those settings.
- If a member is missing, hover the receiver and check its declared type and visibility. A dynamic value
  requires a known type before its members can be offered.
- For a sandbox trace, use `./gradlew runIde -PlspTrace=<existing-folder>` from `editors/jetbrains`.
  Trace files contain source text; keep them outside the repository and do not enable them for timings.
- Windows transport uses binary stdio, pipe readiness checks, UTF-8 filesystem paths and `.exe` compiler
  discovery. Windows runtime validation is still pending; the macOS tests do not establish Windows support.
- Unknown callable signatures cannot show argument help. General incremental declaration checking and
  interruption of a running check remain pending.
- Rider uses its native LSP client. IntelliJ IDEA Community/free unified IDEA uses the optional LSP4IJ
  plugin (0.21.0); install and enable it in **Settings > Plugins**. A project notification explains this
  requirement if it is missing. Local syntax, punctuation pairing and indentation work without it.
  Android Studio is outside the scope. See the [tested builds](jetbrains/README.md#names-and-compatibility-the-owners-to-change).

The [IntelliJ LSP documentation](https://plugins.jetbrains.com/docs/intellij/language-server-protocol.html)
describes the supported products. The [editor extension documentation](https://plugins.jetbrains.com/docs/intellij/additional-minor-features.html)
and [threading guidance](https://plugins.jetbrains.com/docs/intellij/threading-model.html) describe the
platform facilities used here. Validation distinguishes server tests, sandbox editor checks and human
acceptance in the results document.
