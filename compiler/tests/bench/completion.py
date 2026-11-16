#!/usr/bin/env python3
"""Completion latency over stdio, alternating builds, with optional edit/memory soak.

Usage: completion.py BUILD --against BASELINE [--runs 101] [--seconds 600] [--lines 0 100000 1000000]
                     [--mode recovery|body|signature|hub]
Each kind of request is timed for both builds in turn: warm (no edit), edit (one change, then completion),
burst (twenty changes, then completion) and program (one change, then find references, which waits for
every file to be checked). Reports process RSS separately from latency; RSS alone is not retained-heap
evidence.
"""
import argparse
import json
import os
from pathlib import Path
import select
import statistics
import subprocess
import sys
import tempfile
import time


class Client:
    def __init__(self, build, root):
        self.process = subprocess.Popen([str(build / 'kas3c'), 'lsp'], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.buffer = b''
        self.serial = 0
        self.request('initialize', {'rootUri': root.as_uri(), 'capabilities': {'textDocument': {'completion': {'completionList': {'itemDefaults': ['editRange']}}}}})
        self.send('initialized', {})

    def send(self, method, params, serial=None):
        message = {'jsonrpc': '2.0', 'method': method, 'params': params}
        if serial is not None:
            message['id'] = serial
        data = json.dumps(message).encode()
        self.process.stdin.write(b'Content-Length: %d\r\n\r\n' % len(data) + data)
        self.process.stdin.flush()

    def request(self, method, params):
        self.serial += 1
        self.send(method, params, self.serial)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if b'\r\n\r\n' in self.buffer:
                header, body = self.buffer.split(b'\r\n\r\n', 1)
                length = int(header.split(b':', 1)[1])
                if len(body) >= length:
                    message = json.loads(body[:length])
                    self.buffer = body[length:]
                    if message.get('id') == self.serial:
                        if 'error' in message:
                            raise RuntimeError(message['error'])
                        return message.get('result'), length
                    continue
            if select.select([self.process.stdout], [], [], 0.1)[0]:
                chunk = os.read(self.process.stdout.fileno(), 65536)
                if not chunk:
                    raise RuntimeError('language server exited before answering')
                self.buffer += chunk
        raise TimeoutError(method)

    def rss(self):
        return int(subprocess.check_output(['ps', '-o', 'rss=', '-p', str(self.process.pid)]))

    def close(self):
        try:
            self.request('shutdown', None)
            self.send('exit', None)
            self.process.wait(timeout=10)
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait(timeout=10)
            for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                stream.close()


def report(label, values):
    ordered = sorted(values)
    print(f'{label}: median={statistics.median(values):.3f} ms '
          f'p95={ordered[min(len(ordered)-1, int(len(ordered)*.95))]:.3f} ms '
          f'max={max(values):.3f} ms', flush=True)


class Scenario:
    """What is edited, and where completion is asked: the same text and edits for both builds."""

    def __init__(self, folder, mode):
        self.mode = mode
        if mode in ('recovery', 'body'):
            # A long edited file exposes repeated lexical scans.
            text = ('// completion context\n' * 5000 +
                    'package { import godot.*; public class CompletionProbe extends Node2D {\n'
                    'public function run():void { transform.origin.\n} } }')
            if mode == 'body':
                text = text.replace('transform.origin.\n', 'transform.origin.x; /* edit */\n')
            self.asked = folder / 'CompletionProbe.as'
            self.asked_text = text
            self.position = {'line': 5001, 'character': len('public function run():void { transform.origin.')}
            self.label = 'x'
            self.edited, self.edited_text = self.asked, text
            return
        # A file of the generated project, in the middle of the ring: Thing<n-1> calls its methods.
        things = sorted(folder.glob('game/Thing*.as'), key=lambda path: int(path.stem[5:]))
        self.asked = things[len(things) // 2]
        self.asked_text = self.asked.read_text(encoding='utf-8')
        line = next(i for i, text in enumerate(self.asked_text.split('\n')) if 'var moved:Vector2 = shift' in text)
        self.position = {'line': line, 'character': len('\t\tvar moved:Vector2 = shift')}
        self.label = 'shift'
        self.edited = self.asked if mode == 'signature' else folder / 'game' / 'Hub.as'
        self.edited_text = self.edited.read_text(encoding='utf-8')

    def edit(self, serial):
        """The edited file's text for one edit: each edit undoes the one before."""
        if self.mode == 'recovery':
            return self.edited_text + '\n' * (1 + serial % 2)
        if self.mode == 'body':
            return self.edited_text.replace('/* edit */', '/* edit %d */' % (serial % 2))
        if serial % 2 == 0:
            return self.edited_text
        if self.mode == 'signature':
            return self.edited_text.replace('step0(delta:Number, amount:int)', 'step0(delta:Number, amount:int, extra:int = 0)')
        return self.edited_text.replace('tick(n:int):int', 'tick(n:Number):int')


def measure(builds, folder, runs, seconds, mode):
    clients = []
    scenario = Scenario(folder, mode)
    uri = scenario.asked.as_uri()
    edited_uri = scenario.edited.as_uri()
    params = {'textDocument': {'uri': uri}, 'position': scenario.position}
    # After an edit, find references waits for every file: the time until the whole program is checked.
    # In recovery the caret is after a dot, where no name is to find references of.
    kinds = ('warm', 'edit', 'burst') if mode in ('recovery', 'body') else ('warm', 'edit', 'burst', 'program')
    samples = {name: {kind: [] for kind in kinds} for name in builds}
    try:
        for name, build in builds.items():
            start = time.perf_counter()
            client = Client(build, folder)
            clients.append((name, client))
            client.send('textDocument/didOpen', {'textDocument': {
                'uri': uri, 'version': 1, 'languageId': 'actionscript', 'text': scenario.asked_text}})
            if edited_uri != uri:
                client.send('textDocument/didOpen', {'textDocument': {
                    'uri': edited_uri, 'version': 1, 'languageId': 'actionscript', 'text': scenario.edited_text}})
            result, size = client.request('textDocument/completion', params)
            print(f'{name} cold={1000*(time.perf_counter()-start):.3f} ms '
                  f'items={len(result["items"])} bytes={size} rss={client.rss()} KiB', flush=True)
        version = 1
        for kind in kinds:
            for iteration in range(runs):
                version += 1
                for name, client in (clients if iteration % 2 == 0 else reversed(clients)):
                    start = time.perf_counter()
                    if kind != 'warm':
                        for burst in range(20 if kind == 'burst' else 1):
                            client.send('textDocument/didChange', {'textDocument': {'uri': edited_uri, 'version': version*20+burst},
                                        'contentChanges': [{'text': scenario.edit(version+burst)}]})
                    if kind == 'program':
                        client.request('textDocument/references', dict(params, context={'includeDeclaration': True}))
                    else:
                        result, _ = client.request('textDocument/completion', params)
                        assert any(item['label'] == scenario.label for item in result['items']), result
                    samples[name][kind].append(1000*(time.perf_counter()-start))
            for name in builds:
                report(f'{name} {kind}', samples[name][kind])
        start = time.monotonic()
        next_sample = 0
        while time.monotonic() - start < seconds:
            version += 1
            for name, client in clients:
                client.send('textDocument/didChange', {'textDocument': {'uri': edited_uri, 'version': version*20},
                            'contentChanges': [{'text': scenario.edit(version)}]})
                result, _ = client.request('textDocument/completion', params)
                assert any(item['label'] == scenario.label for item in result['items'])
            elapsed = time.monotonic() - start
            if elapsed >= next_sample:
                print(f'soak {elapsed:.0f}s cycles={version}: ' +
                      ', '.join(f'{name} rss={client.rss()} KiB' for name, client in clients), flush=True)
                next_sample += 60
            time.sleep(.1)
    finally:
        for _, client in clients:
            client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build', type=Path)
    parser.add_argument('--against', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=101)
    parser.add_argument('--seconds', type=int, default=0)
    parser.add_argument('--lines', type=int, nargs='+', default=[0, 100000])
    parser.add_argument('--mode', choices=('recovery', 'body', 'signature', 'hub'), default='recovery',
                        help='recovery: an unfinished member expression in a long file of its own; body: a comment in '
                             'a valid method there; signature: a method\'s parameters in a generated file that '
                             'another calls; hub: the method of Hub.as that every generated file calls')
    parser.add_argument('--valid-body', action='store_true', help='The same as --mode body')
    args = parser.parse_args()
    mode = 'body' if args.valid_body else args.mode
    builds = {'before': args.against.resolve(), 'after': args.build.resolve()}
    for lines in args.lines:
        if mode in ('signature', 'hub') and not lines:
            continue
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            if lines:
                subprocess.run([sys.executable, str(Path(__file__).with_name('generate_project.py')),
                                temporary, str(lines), '--hub'], check=True, stdout=subprocess.DEVNULL)
            print(f'project generated_lines={lines}, mode={mode}, Godot loaded; '
                  f'load average {" ".join("%.1f" % value for value in os.getloadavg())}', flush=True)
            measure(builds, folder, args.runs, args.seconds if lines else 0, mode)


if __name__ == '__main__':
    main()
