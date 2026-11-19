"""Length-framed AS3 events: arbitrary labels cannot inject runner messages."""
from collections import namedtuple
import json
import re

PREFIX_V1 = "@kas3-test-v1 "
PREFIX_V2 = "@kas3-test-v2 "
PREFIX = PREFIX_V1
HEADER_RE = re.compile(r"@kas3-test-v([12]) (\w+) (\d+) (\d+)")
Event = namedtuple("Event", "kind name details meta")


def _utf16_units(text):
    return sum(2 if ord(ch) > 0xFFFF else 1 for ch in text)


def _take_field(buf, units):
    if units <= len(buf):
        prefix = buf[:units]
        if prefix.isascii():
            if units >= len(buf):
                return None
            if buf[units] != "\n":
                raise ValueError("Truncated AS3 test event")
            return prefix, buf[units + 1:]
    end = 0
    remaining = units
    while remaining > 0 and end < len(buf):
        remaining -= 2 if ord(buf[end]) > 0xFFFF else 1
        end += 1
    if remaining > 0 or end >= len(buf):
        return None
    if buf[end] != "\n":
        raise ValueError("Truncated AS3 test event")
    return buf[:end], buf[end + 1:]


class StreamParser:
    """Incremental line-or-chunk parser for @kas3-test-v1 and @kas3-test-v2 streams."""

    def __init__(self):
        self._buf = ""
        self._state = "scan"
        self._version = 1
        self._kind = ""
        self._header_size = 0
        self._details_size = 0
        self._header_text = ""
        self._suite = None
        self._test = None
        self._names = set()
        self._failed = False
        self._count = 0

    def feed(self, chunk):
        """Feed any text chunk (e.g. one line from stdout) and yield ('output', line) or ('event', Event)."""
        if (
            self._state == "scan"
            and not self._buf
            and chunk.endswith("\n")
            and PREFIX_V1 not in chunk
            and PREFIX_V2 not in chunk
        ):
            for line in chunk.splitlines():
                yield ("output", line)
            return
        self._buf += chunk
        while self._buf:
            if self._state == "scan":
                idx = self._find_prefix(self._buf)
                if idx < 0:
                    nl = self._buf.rfind("\n")
                    if nl < 0:
                        break
                    out = self._buf[:nl + 1]
                    self._buf = self._buf[nl + 1:]
                    for line in out.splitlines():
                        yield ("output", line)
                    break
                if idx > 0:
                    prefix_text = self._buf[:idx]
                    self._buf = self._buf[idx:]
                    for line in prefix_text.splitlines():
                        yield ("output", line)
                nl = self._buf.find("\n")
                if nl < 0:
                    break
                line = self._buf[:nl]
                if line.endswith("\r"):
                    line = line[:-1]
                header = HEADER_RE.fullmatch(line)
                if header is None:
                    raise ValueError("Malformed AS3 test event")
                version, kind, header_size, details_size = header.groups()
                self._version = int(version)
                self._kind = kind
                self._header_size = int(header_size)
                self._details_size = int(details_size)
                if self._header_size > 65536 or self._details_size > 1048576:
                    raise ValueError("AS3 test event is too large")
                self._buf = self._buf[nl + 1:]
                self._state = "header"
            elif self._state == "header":
                taken = _take_field(self._buf, self._header_size)
                if taken is None:
                    break
                self._header_text, self._buf = taken
                self._state = "details"
            elif self._state == "details":
                taken = _take_field(self._buf, self._details_size)
                if taken is None:
                    break
                details, self._buf = taken
                self._state = "scan"
                yield ("event", self._validate(self._version, self._kind, self._header_text, details))

    def finish(self):
        """Flush any trailing non-protocol text and verify no truncated or unclosed events remain."""
        if self._state != "scan":
            raise ValueError("Truncated AS3 test event")
        if self._buf:
            idx = self._find_prefix(self._buf)
            if idx >= 0:
                raise ValueError("Malformed AS3 test event")
            trailing = self._buf
            self._buf = ""
            for line in trailing.splitlines():
                yield ("output", line)
        if self._suite is not None or self._test is not None:
            raise ValueError("AS3 suite stopped before completion")

    @staticmethod
    def _find_prefix(text):
        i1 = text.find(PREFIX_V1)
        i2 = text.find(PREFIX_V2)
        if i1 < 0:
            return i2
        if i2 < 0:
            return i1
        return min(i1, i2)

    def _validate(self, version, kind, header_text, details):
        if version == 2:
            try:
                meta = json.loads(header_text)
            except json.JSONDecodeError as error:
                raise ValueError("Malformed AS3 test event metadata") from error
            if not isinstance(meta, dict):
                raise ValueError("Malformed AS3 test event metadata")
            name = meta.get("name")
            if not isinstance(name, str):
                raise ValueError("Test names must be nonempty strings")
        else:
            name = header_text
            meta = {"name": name}
        if not name or "::" in name or "\n" in name or "\r" in name:
            raise ValueError("Test names must be single-line and cannot contain ::")
        if kind == "suiteStarted" and self._suite is None and self._test is None:
            self._suite = name
            self._names = set()
        elif kind == "testStarted" and self._suite is not None and self._test is None and name not in self._names:
            self._test = name
            self._names.add(name)
            self._failed = False
        elif kind == "testIgnored" and self._suite is not None and self._test is None and name not in self._names:
            self._names.add(name)
        elif kind == "testFailed" and self._test == name and not self._failed:
            self._failed = True
        elif kind == "testFinished" and self._test == name and self._test is not None:
            self._test = None
        elif kind == "suiteFinished" and self._suite == name and self._test is None and self._names:
            self._suite = None
        else:
            raise ValueError("Out-of-order AS3 test event: " + kind)
        self._count += 1
        if self._count > 100000:
            raise ValueError("Too many AS3 test events")
        return Event(kind, name, details, meta)


def parse_events(output):
    parser = StreamParser()
    events = [item for kind, item in parser.feed(output) if kind == "event"]
    for _ in parser.finish():
        pass
    return events


def parse(output):
    return [(event.kind, event.name, event.details) for event in parse_events(output)]
