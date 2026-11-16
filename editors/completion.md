# Completion and typing assistance

Kumikae uses the compiler's names and types for completion. Rider's native LSP client, IntelliJ IDEA
through LSP4IJ, and VS Code use the same `kas3c lsp` implementation. Local punctuation and indentation
use editor handlers; typing a bracket does not wait for the language server.

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
inaccessible signature types and non-public member defaults are excluded, and so is a method this class
overrides already. An explicitly written
visibility must match the inherited method. Public default constants work when written as `VALUE`,
`Base.VALUE` or `package.name.Base.VALUE`; generated references retain their meaning in the derived class.
Getters/setters use their corresponding accessor prefix. Overriding one does not hide the other, and
`final` applies to the selected accessor. Completion also works immediately after `override function `,
`override function get ` or `override function set `, before any name character is typed. A member on
the following line, including its modifiers and metadata, remains intact.

Completion starts as soon as `override` is written, with or without the access word: before `function`
it offers methods and properties alike, and accepting one also writes `function` (and `get` or `set` for
a property's half). Inside a function body `override` offers no stubs.

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
inside comments and strings is not treated as a new code block. The local lexer tracks expression and statement braces, control conditions, function declarations and
expressions, ternary expressions and labels to distinguish regex literals from division. Its restart
state fits the sixteen bits the editor keeps and carries a class's members' nesting directly, so an edit
there is lexed again from the member. Deeper inside, the editor restarts at the member; a restart there
anyway reconstructs context from the text prefix, without retaining a document history. The compiler
supplies its own parsed regions for completion and argument help.

Highlighting still comes from the shared TextMate grammar. A small adapter supplies AS3 token boundaries
to the native editing handlers while preserving TextMate's color attributes, including string escapes.

## Performance and ownership

The parser records non-code text intervals once; editor queries search those intervals instead of
scanning the file from its beginning on every request. The resolver's scopes belong to the current
compilation. Completion caches declaration descriptions and a compact list of relevant syntax nodes
within that same compilation. An edit releases those caches before rechecking; no old syntax pointers
or history of prefix results survives the generation. A file whose bodies are checked again drops only
what was worked out from it.

Clients advertising LSP completion-list edit defaults receive the shared range once. Older clients
receive a range with each item. Snippets and separate insert/replace edits are also capability-gated.
Every result describes the document as it was when the request was made.

An edit checks again only what it can change. The program is checked in two layers:

- **Declarations**, what other files can see: classes and interfaces with their bases, members, accessors,
  signatures and imports, including the imports a function body writes for its own signature. The edited
  file's declarations are checked at once, and each declaration gets a fingerprint: its text outside any
  body, and what its written types resolved to. A file is checked again when a fingerprint it read
  changed: a declaration it found, a class it uses as a type (its chain of supertypes), a class it extends
  or implements (all its members), a member or package-level name it looked up whether or not anything
  was found, a package it imports whole, another file's position quoted in its messages, or a property
  half its class took from another file. That goes on round by round until nothing changes.
- **Bodies.** Each file's bodies read only what other files declare, so files are checked one at a time,
  in any order. A request checks the file it asks about, and the files of its classes' bases (whose
  default values an `override` writes); find references checks every file. The rest are checked when the
  editor pauses. A file whose bodies alone read a change has only its bodies checked again.

Every answer about a checked file is what a check of the whole program would give: a random-edit oracle
compares the two after each of thousands of edits (see the [follow-up results](../docs/results/2026-11-completion-followup.md)).
A syntax error is confined to its file: its declarations, as the parser recovered them, are compared like
those of any edit, so files that read nothing it changed are not checked again; they are only judged again
for what a file that does not parse makes doubtful. Declarations that are gone leave their symbols to be
used again by the next ones, so a long session does not grow. A file added to or removed from the program,
an edited `--declarations` file, or another project folder starts the declarations over; bodies are then
checked as above.

Document changes with old or duplicate versions are ignored with a log message. Diagnostic versions are
sent when the client advertises support, including empty diagnostic lists for each newly checked clean
version. If typing overtakes an earlier clear, the next clean version clears stale errors again.
Rider receives diagnostic versions and ranges atomically with respect to document writes, so a reply
cannot cache pre-edit offsets while a deletion is still in progress.
A bounded lookahead batch skips a queued request that is cancelled, or whose document is closed, before
checking. A request that a newer edit follows is still answered, for the text it was asked about: LSP 3.17
says a server should not send ContentModified for a change it has only queued, and LSP4IJ does not ask again
after one (which once left override suggestions unshown). Partial frames do not delay an
already complete request. Answers are sent before anything else is checked. The check of what is left,
while the editor pauses, takes the files open in the editor first and stops after any file when a message
arrives: the message is handled first (an edit checks declarations, a request its own file, for the text it
was asked about), and the check goes on from where it stopped. Diagnostics are published for each file as
it is checked; a file not yet checked keeps what the editor has. While a file does not parse, the others
are judged again for what that makes doubtful only when a declaration or the set of broken files changed. Cancellation and closing a
document affect queued requests as before.
See the [follow-up results](../docs/results/2026-11-completion-followup.md) for latency, memory and validation.

## Troubleshooting and limits

- If all language features are absent, check that the plugin can find `kas3c`. The JetBrains plugin
  uses a configured compiler first, then a build in the project or its ancestors, then its bundled
  compiler. Rebuilding an unrelated checkout does not update an installed plugin's binary.
- If pairing is disabled, check the IDE's editor smart-key settings. The plugin honors those settings.
- If a member is missing, hover the receiver and check its declared type and visibility. A dynamic value
  offers only `Object`'s members until it has a known type.
- For a sandbox trace, use `./gradlew runIde -PlspTrace=<existing-folder>` from `editors/jetbrains`.
  Trace files contain source text; keep them outside the repository and do not enable them for timings.
- Windows transport uses binary stdio, pipe readiness checks, UTF-8 filesystem paths and `.exe` compiler
  discovery, and Windows paths are tested on every system. The server has not yet been built or run on
  Windows itself.
- Unknown callable signatures cannot show argument help.
- An edit to a declaration that every file reads (a method of a class all files call) checks every file's
  bodies again in the background: a request is still answered at once, and errors arrive file by file.
  A file added to or removed from the project starts the declarations of the whole program over.
- Rider uses its native LSP client. IntelliJ IDEA Community/free unified IDEA uses the optional LSP4IJ
  plugin (0.21.0); install and enable it in **Settings > Plugins**. A project notification explains this
  requirement if it is missing. Local syntax, punctuation pairing and indentation work without it.
  Restart the IDE after installing or updating Kumikae AS3. Rider keeps its split Problems view.
  Android Studio is outside the scope. See the [tested builds](jetbrains/README.md#names-and-compatibility-the-owners-to-change).

The [IntelliJ LSP documentation](https://plugins.jetbrains.com/docs/intellij/language-server-protocol.html)
describes the supported products. The [editor extension documentation](https://plugins.jetbrains.com/docs/intellij/additional-minor-features.html)
and [threading guidance](https://plugins.jetbrains.com/docs/intellij/threading-model.html) describe the
platform facilities used here. Validation distinguishes server tests, sandbox editor checks and human
acceptance in the results document.
