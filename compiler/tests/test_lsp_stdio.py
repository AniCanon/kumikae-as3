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


document_version = 0


def opened(text):
    global document_version
    document_version = 1
    return notify("textDocument/didOpen", {"textDocument": {"uri": URI, "languageId": "actionscript", "version": 1,
                                                             "text": text}})


def changed(text):
    global document_version
    document_version += 1
    return notify("textDocument/didChange", {"textDocument": {"uri": URI, "version": document_version},
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

# A client may discard a clean version while typing ahead. The next clean version must clear it too.
process = subprocess.Popen([KAS3C, "lsp", "--no-godot"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
try:
    process.stdin.write(request(1, "initialize", {"capabilities": {
        "textDocument": {"publishDiagnostics": {"versionSupport": True}}}}) + notify("initialized", {}))
    good = "package { public class Main { public function later():void {} } }"
    edits = [opened("package { public class Main { public function later():void } }"),
             changed(good), changed(good + " ")]
    for version, edit in enumerate(edits, 1):
        process.stdin.write(edit)
        process.stdin.flush()
        collected = b""
        published = []
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and not published:
            if select.select([process.stdout], [], [], 0.2)[0]:
                chunk = os.read(process.stdout.fileno(), 65536)
                if not chunk:
                    break
                collected += chunk
                messages = unframe(collected)
                if messages is not None:
                    published = diagnostics(messages)
        check("diagnostic clear version %d" % version,
              len(published) == 1 and published[0].get("version") == version and
              published[0]["uri"] == URI and bool(published[0]["diagnostics"]) == (version == 1),
              repr(published))
finally:
    process.stdin.write(END)
    process.stdin.close()
    check("diagnostic clear exit", process.wait(timeout=20) == 0)
    process.stdout.close()
    process.stderr.close()

# Editors' clients say how to talk by adding --stdio (VS Code's does): it is the only way, and accepted.
code, messages, errors = session(START + END, "--no-godot", "--stdio")
check("--stdio", code == 0 and messages is not None and len(messages) == 2 and errors == b"", repr((code, errors)))

# White space after the length is allowed.
spaced = json.dumps({"jsonrpc": "2.0", "id": 8, "method": "workspace/symbol", "params": {"query": ""}}).encode()
spaced = b"Content-Length: %d  \r\n\r\n" % len(spaced) + spaced
code, messages, errors = session(START + spaced + END, "--no-godot")
check("length with a space", code == 0 and messages is not None and any(m.get("id") == 8 for m in messages), repr((code, messages)))

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
        places = found[0].get("result") if found else None
        target = places[0]["uri"] if isinstance(places, list) and len(places) == 1 else ""
        check("godot definition", target.startswith("file://") and target.endswith("godot.as") and
              os.path.exists(target[len("file://"):].replace("%20", " ")), repr(found))
        symbols = [m for m in messages if m.get("id") == 3]
        check("symbols", len(symbols) == 1 and [s["name"] for s in symbols[0]["result"]] == ["Player"] and
              symbols[0]["result"][0]["location"]["uri"] == uri, repr(symbols))

# Completion insertion and argument help over the actual framed transport.
completion_text = ('package { public class Main { public function move(amount:int, label:String = "x"):void {} '
                   'public function run():void { movsuffix; move(1, ); } } }')
caps = {"textDocument": {"completion": {"completionItem": {"snippetSupport": True, "insertReplaceSupport": True}}}}
start = request(1, "initialize", {"capabilities": caps}) + notify("initialized", {})
position = completion_text.index("movsuffix")
code, messages, errors = session(start + opened(completion_text) +
    request(2, "textDocument/completion", {"textDocument": {"uri": URI}, "position": {"line": 0, "character": position + 3}}) +
    request(3, "textDocument/signatureHelp", {"textDocument": {"uri": URI}, "position": {"line": 0, "character": completion_text.index("move(1") + 8}}) + END,
    "--no-godot")
check("completion session", code == 0 and messages is not None and errors == b"", repr(errors))
if messages:
    reply = next((m for m in messages if m.get("id") == 2), {})
    item = next((i for i in reply.get("result", {}).get("items", []) if i.get("label") == "move"), {})
    check("snippet", item.get("textEdit", {}).get("newText") == "move(${1:amount})$0", repr(item))
    check("replace suffix", item.get("textEdit", {}).get("replace", {}).get("end", {}).get("character") == position + 9, repr(item))
    signature = next((m for m in messages if m.get("id") == 3), {})
    check("signature parameter", signature.get("result", {}).get("activeParameter") == 1, repr(signature))

# Invalid later changes cannot invalidate an otherwise current queued request.
invalid_change = notify("textDocument/didChange", {"textDocument": {"uri": URI, "version": 2147483648},
                         "contentChanges": [{"text": "ignored"}]})
code, messages, errors = session(START + opened("package { public class Main {} }") +
    request(2, "textDocument/completion", {"textDocument": {"uri": URI}, "position": {"line": 0, "character": 0}}) +
    invalid_change + END, "--no-godot")
check("invalid change keeps queued request", any(m.get("id") == 2 and "result" in m for m in messages or []))

# An incomplete following frame must not hold a completed request hostage.
for partial in (b"C", b"Content-Length: 100\r\n\r\n{"):
    process = subprocess.Popen([KAS3C, "lsp", "--no-godot"], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(START + partial)
        process.stdin.flush()
        ready = bool(select.select([process.stdout], [], [], 2)[0])
        check("response before partial next frame", ready, repr(partial))
        if ready:
            check("initialize before partial next frame", b'"id":1' in os.read(process.stdout.fileno(), 65536))
    finally:
        process.stdin.close()
        process.wait(timeout=10)
        process.stdout.close()
        process.stderr.close()

# Empty-prefix lists must refresh when withheld imports become eligible.
source = "package { public class Main { var point:; } }"
code, messages, errors = session(START + opened(source) + request(2, "textDocument/completion",
    {"textDocument": {"uri": URI}, "position": {"line": 0, "character": source.index(":") + 1}}) + END)
check("empty prefix refresh", any(m.get("id") == 2 and m.get("result", {}).get("isIncomplete") is True for m in messages or []))

# Generated edits preserve CRLF and keep a single-line primary range with list defaults.
for kind, text, label in (
    ("import", "package {\r\npublic class Main { var point:Vec|; } }", "Vector2"),
    ("override", "package { import godot.*; public class Main extends Node2D { override\r\nfunction _ph| } }", "_physicsProcess"),
):
    caret = text.index("|")
    source = text.replace("|", "")
    before = source[:caret]
    pos = {"line": before.count("\n"), "character": len(before.rsplit("\n", 1)[-1])}
    caps = {"textDocument": {"completion": {"completionItem": {"snippetSupport": True},
            "completionList": {"itemDefaults": ["editRange"]}}}}
    start = request(1, "initialize", {"capabilities": caps}) + notify("initialized", {})
    code, messages, errors = session(start + opened(source) + request(2, "textDocument/completion",
        {"textDocument": {"uri": URI}, "position": pos}) + END)
    reply = next((m for m in messages or [] if m.get("id") == 2), {})
    result = reply.get("result", {})
    item = next((i for i in result.get("items", []) if i.get("label") == label), {})
    edits = item.get("additionalTextEdits", [])
    check(kind + " additional edit", len(edits) == 1, repr(item))
    if kind == "import":
        check("import CRLF", bool(edits) and "import godot.Vector2;\r\n" in edits[0]["newText"], repr(item))
    else:
        primary = item.get("textEdit", {})
        edit_range = primary.get("range", {})
        check("override single line", edit_range.get("start", {}).get("line") == pos["line"] ==
              edit_range.get("end", {}).get("line"), repr(item))
        check("override snippet", primary.get("newText", "").startswith("_physicsProcess(") and
              "$0throw" in primary.get("newText", "") and item.get("insertTextFormat") == 2, repr(item))
        check("override literal closing brace", primary.get("newText", "") ==
              '_physicsProcess(delta:Number):void {\r\n\t$0throw new Error("Not implemented");\r\n}', repr(item))

# Language templates negotiate snippets separately from shared completion-list ranges.
for snippets in (False, True):
    for defaults in (False, True):
        source = "package { class Main { function run():void { trsuffix } } }"
        start_at = source.index("trsuffix")
        caps = {"textDocument": {"completion": {"completionItem": {"snippetSupport": snippets},
                "completionList": {"itemDefaults": ["editRange"] if defaults else []}}}}
        start = request(1, "initialize", {"capabilities": caps}) + notify("initialized", {})
        code, messages, errors = session(start + opened(source) + request(2, "textDocument/completion",
            {"textDocument": {"uri": URI}, "position": {"line": 0, "character": start_at + 2}}) + END)
        result = next((m.get("result", {}) for m in messages or [] if m.get("id") == 2), {})
        item = next((i for i in result.get("items", []) if i.get("label") == "try"), {})
        edit = item.get("textEdit", {})
        expected = 'try {\n\t$1\n} catch (${2:error}:${3:Error}) {\n\t$4throw ${2:error};\n}$0' if snippets else "try"
        check("try template text", edit.get("newText") == expected, repr(item))
        check("try template format", item.get("insertTextFormat", 1) == (2 if snippets else 1), repr(item))
        check("try template replacement", edit.get("range") == {"start": {"line": 0, "character": start_at},
              "end": {"line": 0, "character": start_at + len("trsuffix")}}, repr(item))

# Requests already cancelled or superseded in the input queue must not check old text.
params = {"textDocument": {"uri": URI}, "position": {"line": 0, "character": 0}}
code, messages, errors = session(START + opened("package { public class Main {} }") +
    request(2, "textDocument/completion", params) + notify("$/cancelRequest", {"id": 2}) +
    request(3, "textDocument/completion", params) + changed("package { public class Main { var n:int; } }") +
    request(4, "textDocument/completion", params) + END, "--no-godot")
replies = {m.get("id"): m for m in messages or [] if "id" in m}
check("cancel queued request", replies.get(2, {}).get("error", {}).get("code") == -32800, repr(replies.get(2)))
check("superseded queued request", replies.get(3, {}).get("error", {}).get("code") == -32801, repr(replies.get(3)))
check("latest queued request", "result" in replies.get(4, {}), repr(replies.get(4)))

for failure in failures:
    print("FAIL " + failure)
print("lsp stdio: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
