"""Length-framed AS3 events: arbitrary labels cannot inject runner messages."""
import re

PREFIX = "@kas3-test-v1 "


def field(output, start, units):
    end = start
    while units > 0 and end < len(output):
        units -= 2 if ord(output[end]) > 0xFFFF else 1
        end += 1
    if units != 0 or end >= len(output) or output[end] != "\n":
        raise ValueError("Truncated AS3 test event")
    return output[start:end], end + 1


def parse(output):
    events = []
    cursor = 0
    suite = None
    test = None
    names = set()
    failed = False
    for_guard = 0
    while cursor < len(output):
        start = output.find(PREFIX, cursor)
        if start < 0:
            break
        end = output.find("\n", start)
        header = re.fullmatch(r"@kas3-test-v1 (\w+) (\d+) (\d+)", output[start:end])
        if end < 0 or header is None:
            raise ValueError("Malformed AS3 test event")
        kind, name_size, message_size = header.groups()
        name_size, message_size = int(name_size), int(message_size)
        if name_size > 65536 or message_size > 1048576:
            raise ValueError("AS3 test event is too large")
        name, cursor = field(output, end + 1, name_size)
        message, cursor = field(output, cursor, message_size)
        if not name or "::" in name or "\n" in name or "\r" in name:
            raise ValueError("Test names must be single-line and cannot contain ::")
        if kind == "suiteStarted" and suite is None and test is None:
            suite = name
            names = set()
        elif kind == "testStarted" and suite is not None and test is None and name not in names:
            test = name
            names.add(name)
            failed = False
        elif kind == "testFailed" and test == name and not failed:
            failed = True
        elif kind == "testFinished" and test == name and test is not None:
            test = None
        elif kind == "suiteFinished" and suite == name and test is None and names:
            suite = None
        else:
            raise ValueError("Out-of-order AS3 test event: " + kind)
        events.append((kind, name, message))
        for_guard += 1
        if for_guard > 100000:
            raise ValueError("Too many AS3 test events")
    if suite is not None or test is not None:
        raise ValueError("AS3 suite stopped before completion")
    return events
