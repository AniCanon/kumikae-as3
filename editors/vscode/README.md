# `editors/vscode/`: the VS Code client

A thin client: it starts `kas3c lsp` and adds syntax colouring. Everything else (errors, definitions,
references, hover, outline) is the language server's. Installing it is in [editors/README.md](../README.md).

| File | Job |
|---|---|
| `extension.js` | Starts the server (`kas3.serverPath`) with `vscode-languageclient` |
| `syntaxes/actionscript.tmLanguage.json`, `language-configuration.json` | Colouring, brackets, comments |
| `package.json`, `package-lock.json` | The one dependency, pinned |

## Rules

- **No language knowledge here.** If the editor should know something about AS3, the server tells it.
- Plain JavaScript, no build step, one dependency. Do not add a bundler, TypeScript or a second package.
- `node_modules/` is not committed.
