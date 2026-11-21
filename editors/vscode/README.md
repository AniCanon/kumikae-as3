# `editors/vscode/`: the VS Code client

Requires VS Code 1.82 or newer. The client starts `kas3c lsp` and adds syntax coloring. Everything else, from errors to completion,
comes from the language server. The server registers the workspace file watcher over LSP.

To install and use it, see [Editor support](../README.md#vs-code). This page is for someone changing the
client.

| File | Job |
|---|---|
| `extension.js` | Starts the server with `vscode-languageclient`, and restarts it when a `kas3` setting changes |
| `package.json`, `package-lock.json` | The settings the client declares, and its one dependency, pinned |
| `syntaxes/actionscript.tmLanguage.json` | Coloring: keywords, strings, comments and numbers. The JetBrains plugin uses the same grammar |
| `language-configuration.json` | Brackets and comments |

## Rules

- **No language knowledge here.** If the editor should know something about AS3, the server tells it.
- **Plain JavaScript, no build step, one dependency.** Don't add a bundler, TypeScript or a second
  package.
- **Don't commit `node_modules/`.**

## Tests

Run the unit tests with Node's built-in test runner:

```sh
node --test
```
