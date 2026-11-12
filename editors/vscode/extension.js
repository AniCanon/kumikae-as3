// The VS Code client for kas3c's language server. It starts `kas3c lsp` and connects it to .as files.
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

function start() {
  const settings = vscode.workspace.getConfiguration("kas3");
  const args = ["lsp"];
  if (!settings.get("godot", true)) {
    args.push("--no-godot");
  }
  for (const file of settings.get("declarations", [])) {
    args.push("--declarations", file);
  }
  const server = { command: settings.get("serverPath", "kas3c"), args, transport: TransportKind.stdio };
  client = new LanguageClient("kas3", "Kumikae AS3", server, {
    documentSelector: [{ scheme: "file", language: "actionscript" }],
    synchronize: { fileEvents: vscode.workspace.createFileSystemWatcher("**/*") },
  });
  return client.start();
}

function activate(context) {
  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration(async (event) => {
      if (event.affectsConfiguration("kas3") && client) {
        await client.stop();
        await start();
      }
    })
  );
  return start();
}

function deactivate() {
  return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
