# Simulator protocol, version 1

The panel uses this interface for commands; `simulator.py` uses it for every
operation. It is local HTTP with JSON
(text objects made of named fields). It has no connection to the language server
or the later AS3 debug adapter.

## Connection

The owner-only `session.json` file contains exactly `version`, `port` and `token`.
Read it from the path shown by the panel or standalone service. Never commit it.
Send `POST http://127.0.0.1:<port>/v1` with `Content-Type: application/json` and
`Authorization: Bearer <token>`. Do not send an `Origin` header. Browser-origin
requests are refused. There is no remote host option, proxy, redirect or download.

The panel also uses its in-process session for binary pictures. This is an internal
subscription, not an unauthenticated HTTP endpoint. It shares selection and geometry
with this API. Agent screenshots and all version 1 requests and replies are unchanged.
Live video requires the same owner-controlled idb switch as input.

```json
{"version":1,"id":"request-1","method":"devices","params":{}}
```

`id` is a nonempty string, at most 128 characters. A reply has the same `version`
and `id`, plus either `result` or `error`. An invalid envelope can have a null id.

```json
{"version":1,"id":"request-1","result":[]}
```

```json
{"version":1,"id":"request-2","error":{"code":"INPUT_DISABLED","message":"Input is off. Switch it on in the panel's menu; it needs idb."}}
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
the currently selected device. A session admits one input or slow operation at a
time; status, logs and pictures can proceed independently. Other agents may
change its selection; a stale device request fails instead of reaching a new one.

| Method | `params` | Result |
|---|---|---|
| `devices` | `{}` | Array of `{id,name,runtime,state}` for available iOS devices. |
| `select` | `device`: string | Status. A different selection closes the current console. |
| `status` | `{}` | `device` object or null, `generation`, `inputEnabled`, `pixelsPerPoint`, `inputNotice`, `launch`, optional `continuousTouch`. |
| `boot` | `device` | Status after `bootstatus -b`, with a 120-second limit. |
| `shutdown` | `device` | Status after `simctl shutdown`, with a 120-second limit. |
| `screenshot` | `device`, optional `format`: `png` or `jpeg` (default `png`) | Frame described below. |
| `tap` | `device`, `generation`, `width`, `height`, `x`, `y` | `{sent:true}`. |
| `swipe` | Tap fields plus `endX`, `endY`, `durationMs` | `{sent:true}`. Duration 50–5,000 ms. |
| `text` | `device`, `text`: nonempty printable ASCII | `{sent:true}`. Does not paste Unicode. |
| `button` | `device`, `button`: `home` or `lock` | `{sent:true}`. |
| `rotate` | `device`, `orientation`: `portrait`, `portrait_upside_down`, `landscape_left`, `landscape_right` | `{sent:true}`. Invalidates geometry even if input fails. |
| `install` | `device`, `path`: absolute existing `.app` directory | `{installed:true}` after `simctl` succeeds. 120-second limit. |
| `launch` | `device`, `bundle`: bundle identifier, optional `sources`: absolute folder the game's scripts are edited in (given to the app as `KAS3_SOURCE_DIR`, so it reloads them on save) | Console status plus `state:"starting"`. This acknowledges process creation, not successful app startup. |
| `terminate` | `device`, `bundle` | `{terminated:true}`. Closes the console and ends the app; an app that already ended is not an error. |
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
The idb adapter reads the device's pixel density. Full-size screenshot coordinates
are divided by that density and rounded down to whole points. Internal live pictures
are scaled; their dimensions and generation are validated before coordinates are
mapped to native pixels and then points. Both presentations share the same geometry
generation, so an agent screenshot does not invalidate a live picture. There is one
display and one pointer gesture at a time. `sent:true` means the active backend acknowledged the
input command, not that the application reached an expected state. Check
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
files, stops the HTTP listener, and stops its console process and every native or idb child
it started. `simctl` forwards
signals and may stop that app. The Simulator stays booted. After a crash, an old
descriptor may remain; it is not a live session and must not be reused.

Service error codes include `BAD_REQUEST`, `NO_DEVICE`, `DEVICE_CHANGED`, `DEVICE_UNAVAILABLE`,
`STALE_FRAME`, `INPUT_DISABLED`, `INPUT_UNAVAILABLE`, `BUSY`, `PROCESS_FAILED`,
`TIMEOUT`, `CLOSED` and `INTERNAL_ERROR`. A missing report currently reports an
internal I/O failure; it is not an empty or passing report. Input cannot be enabled
through this protocol. The local process must have been started with the explicit
private-input opt-in described in the README.


## Native helper transport, version 1

This is a local implementation detail, separate from HTTP version 1. The same
remembered private-input switch permits the bundled helper. With it off, pictures
remain public `simctl` screenshots. Startup tries native, installed idb, then
screenshots; the panel notice names the active route and the reason for fallback.
`continuousTouch` is true only for the native route. `pixelsPerPoint` is the last
known density, initially 1 until a picture or explicit input discovers it; clients
must continue to send screenshot pixels with their geometry and generation.

The service starts `simulator-bridge --device <id> --frames <new-absolute-file>`.
It owns stdin and the temporary directory. The helper creates the slot with mode
0600, refuses existing files/symlinks, connects to the selected boot generation,
and validates/publishes its first frame before writing a ready JSON line containing
`ready`, `width`, `height`, and `density`. A startup error writes an `error` and `code` line and exits nonzero.
`DEVICE_UNAVAILABLE` means the selected device is stopped or restarting: retain
the native route for a later reconnect. Other startup failures permit fallback. Closing stdin cancels input even while a command is in flight,
releases held controls, unmaps the slot and releases device objects. The service
reaps the child before deleting its files. Closing a picture only signals its
worker; file and process cleanup happen away from the EDT.

Control lines contain positive `id` and `op`: `ping`, `down`, `move`, `up`, `home`,
`lock`, or `text`. Touch uses finite `x` and `y` in device points. Text is 1–4096
printable ASCII characters. Replies contain `id`, `ok`, `receivedNanos`, and an
`error` on failure. Clients wait for each reply; one pending command is allowed.
The panel coalesces unsent moves and preserves down/up order. Release outside the
screen uses the last valid point; focus loss, hide and disposal release/cancel the
gesture. Coordinates are checked against the shown frame before sending. Input
with unknown delivery is never replayed on another route. Native rotation is
currently unsupported and returns an explicit error; the idb route retains it.

Frames never travel as JSON or base64. Both implementations read
[`frame-layout.json`](native/SimulatorBridge/Sources/SimulatorBridge/Resources/frame-layout.json)
and test against the same golden header. Version 1 is little endian, with a
64-byte header followed by premultiplied BGRA rows. Padding is included in stride.
The file reserves 64 MiB of payload; dimensions are limited to 4096, stride to
16384, density to 1000–10000 milli-pixels per point, and 64-bit values to signed
positive JVM `Long` values. The following table documents that resource rather
than defining another layout in code.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | magic, 827151179; zero while unpublished |
| 4 | 4 | version, 1 |
| 8 | 8 | monotonically increasing sequence |
| 16 | 4 | width |
| 20 | 4 | height |
| 24 | 4 | stride in bytes |
| 28 | 4 | pixel format, 1111970369 (`BGRA`) |
| 32 | 8 | first observation of this changed surface, monotonic nanoseconds |
| 40 | 8 | write timestamp, monotonic nanoseconds |
| 48 | 4 | native width |
| 52 | 4 | native height |
| 56 | 4 | density in milli-pixels per point |
| 60 | 4 | flags, 1 for valid |

The writer takes a nonblocking POSIX whole-file record lock, invalidates the
header, copies pixels, and publishes the complete header last. Contention drops
that source frame. The JVM takes a shared `FileChannel` lock, copies only a new
sequence into `TYPE_INT_ARGB_PRE` off the UI thread, and replaces a newest-value
slot. A zero header is skipped, including after interrupted publication. There is
no queue or reuse of an image still being painted. Hidden panels stop the feed.

A startup ping estimates the Swift/JVM monotonic clock offset using the request
midpoint; half the round trip bounds its uncertainty. `observedNanos` is a seed
poll timestamp, **not** the Simulator's presentation timestamp. Observation-to-paint
measurements exclude the interval before observation. A native input reply follows
a **separate** DTUHID barrier message: it establishes ordered progress through the
guest input service's receive queue, not application handling or visual response.
The actual input message has `isBarrier=false`; marking it as a barrier would skip
its dispatch. `receivedNanos` is the helper's command-processing timestamp, not the
guest receipt timestamp. The helper checks state, boot generation,
surface identity and format on every poll. A later stream opening reconnects after
device failure; no Simulator is booted or shut down by the helper.
