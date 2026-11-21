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

test("serverPath is application-scoped and untrusted workspaces are restricted", () => {
  const manifest = require("../package.json");
  assert.equal(manifest.contributes.configuration.properties["kas3.serverPath"].scope, "application");
  assert.equal(manifest.capabilities.untrustedWorkspaces.supported, "limited");
  assert.ok(manifest.capabilities.untrustedWorkspaces.restrictedConfigurations.includes("kas3.serverPath"));
});

test("handles malformed settings and server start rejections cleanly", async () => {
  const errors = [];
  const launched = [];
  const workspace = {
    getConfiguration: () => ({
      get: (key, fallback) => {
        if (key === "serverPath") return "   ";
        if (key === "declarations") return "not-an-array";
        return fallback;
      },
    }),
    onDidChangeConfiguration: () => ({ dispose() {} }),
  };
  const window = { showErrorMessage: msg => errors.push(msg) };
  class LanguageClient {
    constructor(_id, _name, server) { launched.push(server); }
    async start() { throw new Error("spawn ENOENT"); }
    needsStop() { return false; }
    async stop() {}
  }
  const sandbox = {
    module: {},
    require: name => name === "vscode"
      ? { workspace, window }
      : { LanguageClient, TransportKind: { stdio: "stdio" } },
  };
  vm.runInNewContext(fs.readFileSync(path.join(root, "extension.js"), "utf8"), sandbox);
  await sandbox.module.exports.activate({ subscriptions: [] });
  assert.equal(launched.length, 1);
  assert.equal(launched[0].command, "kas3c");
  assert.deepEqual(Array.from(launched[0].args), ["lsp"]);
  assert.equal(errors.length, 1);
  assert.match(errors[0], /spawn ENOENT/);
});
