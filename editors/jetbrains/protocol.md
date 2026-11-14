# Simulator protocol, version 1

The panel and `simulator.py` both use this interface. It is local HTTP with JSON
(text objects made of named fields). It has no connection to the language server
or the later AS3 debug adapter.

## Connection

The owner-only `session.json` file contains exactly `version`, `port` and `token`.
Read it from the path shown by the panel or standalone service. Never commit it.
Send `POST http://127.0.0.1:<port>/v1` with `Content-Type: application/json` and
`Authorization: Bearer <token>`. Do not send an `Origin` header. Browser-origin
requests are refused. There is no remote host option, proxy, redirect or download.

```json
{"version":1,"id":"request-1","method":"devices","params":{}}
```

`id` is a nonempty string, at most 128 characters. A reply has the same `version`
and `id`, plus either `result` or `error`. An invalid envelope can have a null id.

```json
{"version":1,"id":"request-1","result":[]}
```

```json
{"version":1,"id":"request-2","error":{"code":"INPUT_DISABLED","message":"Input needs an explicitly enabled idb installation. Use Device Hub for input."}}
```

HTTP 401 means missing or wrong authentication, 404 a wrong path, 405 a wrong
method, and 413 a request over 65,536 bytes. These transport errors need not carry
a protocol envelope. Normal replies, including operation errors, use HTTP 200.

Reject unknown fields, duplicate JSON keys, unknown methods, missing required
fields, wrong types, non-finite numbers and versions other than 1. Strings may be
at most 4,096 characters and contain no NUL. Integers fit in a signed 64-bit value.
There is no arbitrary process or arbitrary file-read operation.

## Methods

All listed fields are required unless marked optional. `device` is an exact id
returned by `devices`, never the `booted` alias. Except for `select`, it must match
the currently selected device. A session serializes operations. Other agents may
change its selection; a stale device request fails instead of reaching a new one.

| Method | `params` | Result |
|---|---|---|
| `devices` | `{}` | Array of `{id,name,runtime,state}` for available iOS devices. |
| `select` | `device`: string | Status. A different selection closes the current console. |
| `status` | `{}` | `device` object or null, `generation`, `inputEnabled`, `pixelsPerPoint`, `inputNotice`, `launch`. |
| `boot` | `device` | Status after `bootstatus -b`, with a 120-second limit. |
| `shutdown` | `device` | Status after `simctl shutdown`, with a 120-second limit. |
| `screenshot` | `device`, optional `format`: `png` or `jpeg` (default `png`) | Frame described below. |
| `tap` | `device`, `generation`, `width`, `height`, `x`, `y` | `{sent:true}`. |
| `swipe` | Tap fields plus `endX`, `endY`, `durationMs` | `{sent:true}`. Duration 50–5,000 ms. |
| `text` | `device`, `text`: nonempty printable ASCII | `{sent:true}`. Does not paste Unicode. |
| `button` | `device`, `button`: `home` or `lock` | `{sent:true}`. |
| `rotate` | `device`, `orientation`: `portrait`, `portrait_upside_down`, `landscape_left`, `landscape_right` | `{sent:true}`. Invalidates geometry even if input fails. |
| `install` | `device`, `path`: absolute existing `.app` directory | `{installed:true}` after `simctl` succeeds. 120-second limit. |
| `launch` | `device`, `bundle`: bundle identifier | Console status plus `state:"starting"`. This acknowledges process creation, not successful app startup. |
| `log` | `cursor`: integer, initially 0 | `{page:{entries,nextCursor,truncated},launch}`. |
| `report` | `device`, `bundle` | `{text}` from only `Documents/as3test.txt` in that app's data container, at most 1 MiB. |

Other Xcode commands have a 15-second limit. The HTTP clients wait up to 130
seconds. Timeouts are not proof that a state-changing operation did nothing.
Request ids correlate replies; they do not deduplicate a retry.

## Frames and coordinates

A frame has `device`, `width`, `height`, `generation`, `format`, `data`,
`capturedAt` and `captureMs`. `data` is a base64-encoded image. `capturedAt` is the
host time in milliseconds since the Unix epoch, sampled after capture.
`captureMs` measures the capture call and image processing; it is not input lag.

Coordinates start at the top left of the image. `0 <= x < width` and
`0 <= y < height`. They are finite numbers in image pixels. `width`, `height` and
`generation` are integers copied from a frame. The service checks them against
its most recently known geometry. A new selection, capture failure, rotation or
dimension change invalidates old geometry. Changes made outside this service are
detected on the next capture; take a fresh screenshot before an agent gesture.

The panel fits the image without stretching and ignores clicks in empty margins.
The idb adapter divides image pixels by the explicitly configured device scale,
rounding down to whole points. One scale is bound to one device. There is one
display and one pointer gesture at a time. `sent:true` means the input command
exited successfully, not that the application reached an expected state. Check
the next screenshot or log.

## Logs and shutdown

Each entry has `cursor`, `stream` and `text`. Streams are `stdout`, `stderr` and
`session`. These describe the console launcher's pipes; Xcode can combine the
app's output. Entries are chunks, not necessarily whole lines. `nextCursor` is
the value to send on the next call. The cursor never resets during a session.
`truncated:true` means some requested history was discarded by the memory limit.
Do not send a future cursor. Recent output from earlier launches stays in the
buffer, with a `session` entry naming each launch.

`launch` status contains `bundle`, `running` and, after process exit, `exitCode`.
They describe the console launcher. Inspect its output and the app's test report
before concluding that the app started or passed its checks.

Closing the IDE project or standalone service removes the endpoint and temporary
files, stops the HTTP listener, and stops its console process. `simctl` forwards
signals and may stop that app. The Simulator stays booted. After a crash, an old
descriptor may remain; it is not a live session and must not be reused.

Service error codes include `BAD_REQUEST`, `NO_DEVICE`, `DEVICE_CHANGED`,
`STALE_FRAME`, `INPUT_DISABLED`, `INPUT_DEVICE_MISMATCH`, `PROCESS_FAILED`,
`TIMEOUT`, `CLOSED` and `INTERNAL_ERROR`. A missing report currently reports an
internal I/O failure; it is not an empty or passing report. Input cannot be enabled
through this protocol. The local process must have been started with the explicit
idb opt-in described in the README.
