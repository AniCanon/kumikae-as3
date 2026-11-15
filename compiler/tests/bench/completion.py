#!/usr/bin/env python3
"""Completion latency over stdio, alternating builds, with optional edit/memory soak.

Usage: completion.py BUILD --against BASELINE [--runs 101] [--seconds 600]
Reports process RSS separately from latency; RSS alone is not retained-heap evidence.
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


def measure(builds, folder, runs, seconds):
    clients = []
    # A long edited file exposes repeated lexical scans; both builds receive identical text.
    source = ('// completion context\n' * 5000 +
              'package { import godot.*; public class CompletionProbe extends Node2D {\n'
              'public function run():void { transform.origin.\n} } }')
    uri = (folder / 'CompletionProbe.as').as_uri()
    position = {'line': 5001, 'character': len('public function run():void { transform.origin.')}
    params = {'textDocument': {'uri': uri}, 'position': position}
    samples = {name: {'warm': [], 'edit': [], 'burst': []} for name in builds}
    try:
        for name, build in builds.items():
            start = time.perf_counter()
            client = Client(build, folder)
            clients.append((name, client))
            client.send('textDocument/didOpen', {'textDocument': {
                'uri': uri, 'version': 1, 'languageId': 'actionscript', 'text': source}})
            result, size = client.request('textDocument/completion', params)
            print(f'{name} cold={1000*(time.perf_counter()-start):.3f} ms '
                  f'items={len(result["items"])} bytes={size} rss={client.rss()} KiB', flush=True)
        version = 1
        for mode in ('warm', 'edit', 'burst'):
            for iteration in range(runs):
                version += 1
                for name, client in (clients if iteration % 2 == 0 else reversed(clients)):
                    start = time.perf_counter()
                    if mode != 'warm':
                        for burst in range(20 if mode == 'burst' else 1):
                            client.send('textDocument/didChange', {'textDocument': {'uri': uri, 'version': version*20+burst},
                                        'contentChanges': [{'text': source + '\n' * (1 + (version+burst) % 2)}]})
                    result, _ = client.request('textDocument/completion', params)
                    assert any(item['label'] == 'x' for item in result['items']), result
                    samples[name][mode].append(1000*(time.perf_counter()-start))
            for name in builds:
                report(f'{name} {mode}', samples[name][mode])
        start = time.monotonic()
        next_sample = 0
        while time.monotonic() - start < seconds:
            version += 1
            for name, client in clients:
                client.send('textDocument/didChange', {'textDocument': {'uri': uri, 'version': version*20},
                            'contentChanges': [{'text': source + '\n' * (1 + version % 2)}]})
                result, _ = client.request('textDocument/completion', params)
                assert any(item['label'] == 'x' for item in result['items'])
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--against', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=101)
    parser.add_argument('--seconds', type=int, default=0)
    args = parser.parse_args()
    builds = {'before': args.against.resolve(), 'after': args.build.resolve()}
    for lines in (0, 100000):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            if lines:
                subprocess.run([sys.executable, str(Path(__file__).with_name('generate_project.py')),
                                temporary, str(lines)], check=True, stdout=subprocess.DEVNULL)
            print(f'project generated_lines={lines}, edited_file=5003 lines, Godot loaded', flush=True)
            measure(builds, folder, args.runs, args.seconds if lines else 0)


if __name__ == '__main__':
    main()
