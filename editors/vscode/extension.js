// The VS Code client for kas3c's language server. It starts `kas3c lsp` and connects it to .as files.
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;
let pending = Promise.resolve();
let generation = 0;
let settingsListener;

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
  });
  return client.start();
}

function restart() {
  const requested = ++generation;
  const apply = async () => {
    if (requested !== generation) return;
    if (client?.needsStop()) await client.stop();
    if (requested === generation) await start();
  };
  pending = pending.then(apply, apply);
  return pending;
}

function activate(context) {
  settingsListener = vscode.workspace.onDidChangeConfiguration(event => {
    if (event.affectsConfiguration("kas3")) return restart();
  });
  context.subscriptions.push(settingsListener);
  return restart();
}

async function deactivate() {
  settingsListener?.dispose();
  ++generation;
  try {
    await pending;
  } finally {
    if (client && client.needsStop()) await client.stop();
  }
}

module.exports = { activate, deactivate };
