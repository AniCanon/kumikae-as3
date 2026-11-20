const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.join(__dirname, "..");

test("the minimum VS Code version supports the packaged language client", () => {
  const manifest = require("../package.json");
  const packages = require("../package-lock.json").packages;
  assert.equal(manifest.engines.vscode, packages["node_modules/vscode-languageclient"].engines.vscode);
  assert.equal(packages[""].engines.vscode, manifest.engines.vscode);
});

test("the server owns the watcher and settings changes cannot overlap client lifetimes", async () => {
  let onSettings, release;
  let pauseOnStop = true;
  const clients = [];
  const workspace = {
    getConfiguration: () => ({ get: (_, fallback) => fallback }),
    onDidChangeConfiguration: callback => { onSettings = callback; return { dispose() {} }; },
    createFileSystemWatcher() { assert.fail("kas3c already registers this watcher over LSP"); }
  };
  class LanguageClient {
    constructor() { clients.push(this); }
    async start() { assert.ok(clients.slice(0, -1).every(client => client.stopped)); }
    needsStop() { return !this.stopped; }
    async stop() {
      assert.ok(!this.stopping);
      this.stopping = true;
      if (pauseOnStop) await new Promise(resolve => { release = resolve; });
      this.stopped = true;
    }
  }
  const sandbox = { module: {}, require: name => name === "vscode" ? { workspace } :
    { LanguageClient, TransportKind: { stdio: "stdio" } } };
  vm.runInNewContext(fs.readFileSync(path.join(root, "extension.js"), "utf8"), sandbox);
  const extension = sandbox.module.exports;
  await extension.activate({ subscriptions: [] });
  const event = { affectsConfiguration: () => true };
  const first = onSettings(event);
  const second = onSettings(event);
  await new Promise(setImmediate);
  pauseOnStop = false;
  release();
  await Promise.all([first, second]);
  assert.equal(clients.length, 2, "a burst of settings changes needs one replacement server");
  pauseOnStop = true;
  const last = onSettings(event);
  await new Promise(setImmediate);
  const shutdown = extension.deactivate();
  release();
  await Promise.all([last, shutdown]);
  assert.ok(clients.every(client => client.stopped));
});
