"""kas3c lsp over its real standard input and output. Usage: test_lsp_stdio.py <path to kas3c>

Everything on standard output must be a framed protocol message; malformed input must end the server
cleanly or be answered with an error.
"""
import json
import os
import subprocess
import sys
import tempfile

KAS3C = sys.argv[1]
failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append("%s %s" % (name, detail))


def frame(message):
    body = json.dumps(message).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def unframe(data):
    """Every message in the bytes, or None if anything else is among them."""
    messages = []
    while data:
        end = data.find(b"\r\n\r\n")
        if end < 0 or not data.startswith(b"Content-Length: "):
            return None
        length = int(data[len(b"Content-Length: "):end])
        body = data[end + 4:end + 4 + length]
        if len(body) != length:
            return None
        messages.append(json.loads(body.decode("utf-8")))
        data = data[end + 4 + length:]
    return messages


def session(data, *args):
    """Sends all the bytes, closes the input and collects what comes back."""
    result = subprocess.run([KAS3C, "lsp", *args], input=data, capture_output=True, timeout=120)
    return result.returncode, unframe(result.stdout), result.stderr


def request(number, method, params=None):
    return frame({"jsonrpc": "2.0", "id": number, "method": method, "params": params})


def notify(method, params=None):
    return frame({"jsonrpc": "2.0", "method": method, "params": params})


def diagnostics(messages):
    return [m["params"] for m in messages if m.get("method") == "textDocument/publishDiagnostics"]


URI = "file:///work/Main.as"
START = request(1, "initialize", {"capabilities": {}}) + notify("initialized", {})
END = request(99, "shutdown") + notify("exit")


def opened(text):
    return notify("textDocument/didOpen", {"textDocument": {"uri": URI, "languageId": "actionscript", "version": 1,
                                                             "text": text}})


def changed(text):
    return notify("textDocument/didChange", {"textDocument": {"uri": URI, "version": 2},
                                             "contentChanges": [{"text": text}]})


# A whole session.
code, messages, errors = session(
    START + opened("package { public class Main { var n:int = \"x\"; } }") +
    request(2, "textDocument/hover", {"textDocument": {"uri": URI}, "position": {"line": 0, "character": 35}}) + END,
    "--no-godot")
check("session exit", code == 0, repr(code))
check("session output", messages is not None and errors == b"", repr(errors))
if messages:
    check("initialize", messages[0].get("id") == 1 and "capabilities" in messages[0].get("result", {}), repr(messages[0]))
    published = diagnostics(messages)
    check("published", len(published) == 1 and published[0]["uri"] == URI and
          published[0]["diagnostics"][0]["message"] == "can't use a value of type 'String' where 'int' is expected",
          repr(published))
    hovers = [m for m in messages if m.get("id") == 2]
    check("hover", len(hovers) == 1 and "var n:int" in hovers[0]["result"]["contents"]["value"], repr(hovers))
    check("shutdown", [m for m in messages if m.get("id") == 99] == [{"jsonrpc": "2.0", "id": 99, "result": None}])

# A burst of changes that arrives at once is checked once, and the last text wins.
burst = START + opened("package { public class Main {} }")
for i in range(40):
    burst += changed("package { public class Main { var v%d:Missing%d; } }" % (i, i))
# The request makes the server answer for the latest text before the session ends.
burst += request(2, "textDocument/documentSymbol", {"textDocument": {"uri": URI}})
code, messages, errors = session(burst + END, "--no-godot")
published = diagnostics(messages or [])
check("burst", code == 0 and len(published) == 1 and
      published[0]["diagnostics"][0]["message"] == "unknown type 'Missing39'", repr(published))

# Bad JSON is answered, and the server carries on. Extra headers and any letter case are fine.
bad = b"Content-Length: 9\r\n\r\n{not json"
odd = json.dumps({"jsonrpc": "2.0", "id": 7, "method": "workspace/symbol", "params": {"query": ""}}).encode()
odd = b"Content-Type: application/vscode-jsonrpc; charset=utf-8\r\ncontent-length: %d\r\n\r\n" % len(odd) + odd
code, messages, errors = session(START + bad + odd + END, "--no-godot")
check("bad json", code == 0 and messages is not None and
      any(m.get("error", {}).get("code") == -32700 for m in messages) and
      any(m.get("id") == 7 and m.get("result") == [] for m in messages), repr(messages))

# Input that ends, or framing that cannot be followed, ends the server without a word on standard output
# beyond what it had already answered.
code, messages, errors = session(START, "--no-godot")
check("end of input", code == 1 and messages is not None and len(messages) == 1, repr((code, messages)))
code, messages, errors = session(b"", "--no-godot")
check("no input", code == 1 and messages == [], repr((code, messages)))
code, messages, errors = session(START + b"Content-Type: x\r\n\r\n{}" + END, "--no-godot")
check("no length", code == 1 and messages is not None and len(messages) == 1, repr((code, messages)))
code, messages, errors = session(START + b"Content-Length: 99999999999999999999\r\n\r\n{}", "--no-godot")
check("huge length", code == 1 and messages is not None and len(messages) == 1, repr((code, messages)))
code, messages, errors = session(START + b"Content-Length: -5\r\n\r\n{}", "--no-godot")
check("negative length", code == 1 and messages is not None, repr((code, messages)))
code, messages, errors = session(START + b"Content-Length: 500\r\n\r\n{\"jsonrpc\"", "--no-godot")
check("short body", code == 1 and messages is not None and len(messages) == 1, repr((code, messages)))
code, messages, errors = session(b"x" * 200000, "--no-godot")
check("noise", code == 1 and messages == [], repr((code, messages)))
code, messages, errors = session(START + notify("exit"), "--no-godot")
check("exit without shutdown", code == 1, repr(code))

# Errors as you type: with nothing asked, the errors arrive once the editor pauses.
import select
import time
process = subprocess.Popen([KAS3C, "lsp", "--no-godot"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
process.stdin.write(START + opened("package { public class Main { var n:int = \"x\"; } }"))
process.stdin.flush()
collected = b""
deadline = time.time() + 20
while time.time() < deadline and b"publishDiagnostics" not in collected:
    if select.select([process.stdout], [], [], 0.2)[0]:
        collected += os.read(process.stdout.fileno(), 65536)
check("unasked", b"publishDiagnostics" in collected and b"can't use a value of type" in collected, repr(collected[-300:]))
process.stdin.write(END)
process.stdin.close()
check("unasked exit", process.wait(timeout=20) == 0)
process.stdout.close()
process.stderr.close()

result = subprocess.run([KAS3C, "lsp", "--bogus"], input=b"", capture_output=True, timeout=60)
check("bad option", result.returncode == 2 and result.stdout == b"" and b"usage: kas3c" in result.stderr, repr(result))

# A project on disk, with Godot's API: clean, and go to definition lands in a file that exists.
with tempfile.TemporaryDirectory() as folder:
    folder = os.path.realpath(folder)
    project = os.path.join(folder, "my game")
    os.makedirs(os.path.join(project, "scripts"))
    open(os.path.join(project, "project.godot"), "w").close()
    path = os.path.join(project, "scripts", "Player.as")
    text = ("package {\nimport godot.*;\npublic class Player extends CharacterBody2D {\n"
            "\toverride public function _physicsProcess(delta:Number):void {\n\t\tmoveAndSlide();\n\t}\n}\n}\n")
    with open(path, "w") as handle:
        handle.write(text)
    uri = "file://" + path.replace(" ", "%20")
    root = "file://" + project.replace(" ", "%20")
    data = (request(1, "initialize", {"rootUri": root, "capabilities": {}}) + notify("initialized", {}) +
            request(2, "textDocument/definition", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 4}}) +
            request(3, "workspace/symbol", {"query": "Player"}) + END)
    code, messages, errors = session(data)
    check("project exit", code == 0 and messages is not None and errors == b"", repr((code, errors)))
    if messages:
        check("project clean", diagnostics(messages) == [], repr(diagnostics(messages)))
        found = [m for m in messages if m.get("id") == 2]
        target = found[0]["result"]["uri"] if found and found[0].get("result") else ""
        check("godot definition", target.startswith("file://") and target.endswith("godot.as") and
              os.path.exists(target[len("file://"):].replace("%20", " ")), repr(found))
        symbols = [m for m in messages if m.get("id") == 3]
        check("symbols", len(symbols) == 1 and [s["name"] for s in symbols[0]["result"]] == ["Player"] and
              symbols[0]["result"][0]["location"]["uri"] == uri, repr(symbols))

for failure in failures:
    print("FAIL " + failure)
print("lsp stdio: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
