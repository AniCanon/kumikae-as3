# Simulator protocol, version 1

This page specifies how a client talks to the Simulator service, and how the service talks to its native
helper. It's for someone writing a client or changing the service.

The service is local HTTP with JSON. The panel uses it for commands, and `simulator.py` uses it for
everything. It has no connection to the language server.

## Connect

The service writes an endpoint file, `session.json`, that only its user can read. The file holds exactly
`version`, `port` and `token`. The panel and the standalone service both show its path. Never commit it.

Send requests like this:

- `POST http://127.0.0.1:<port>/v1`
- `Content-Type: application/json`
- `Authorization: Bearer <token>`

Don't send an `Origin` header: requests from a browser origin are refused. There's no remote host option,
proxy, redirect or download.

A request has a version, an id, a method and parameters:

```json
{"version":1,"id":"request-1","method":"devices","params":{}}
```

`id` is a nonempty string of at most 128 characters. A reply has the same `version` and `id`, and either
`result` or `error`:

```json
{"version":1,"id":"request-1","result":[]}
```

```json
{"version":1,"id":"request-2","error":{"code":"INPUT_DISABLED","message":"Input is off. Switch it on in the panel's menu."}}
```

| HTTP status | Meaning |
|---|---|
| 200 | A reply, including a reply that carries an operation's error |
| 401 | Missing or wrong authentication |
| 404 | A wrong path |
| 405 | A wrong method |
| 413 | A request over 65,536 bytes |

The service rejects unknown fields, duplicate JSON keys, unknown methods, missing fields, wrong types,
non-finite numbers, and versions other than 1. A string can have at most 4,096 characters and no NUL.
Integers fit in a signed 64-bit value. No method runs an arbitrary process or reads an arbitrary file.

## Methods

Every listed field is required unless it's marked optional.

`device` is an exact id returned by `devices`, never the `booted` alias. Except for `select`, it must match
the selected device. If another client changed the selection, the request fails instead of reaching the
new device.

A session admits one input or slow operation at a time. Status, logs and pictures proceed independently.

| Method | `params` | Result |
|---|---|---|
| `devices` | `{}` | An array of `{id,name,runtime,state}` for the available iOS devices |
| `select` | `device` | The status. Selecting another device closes the current console |
| `status` | `{}` | `device` (an object or null), `generation`, `inputEnabled`, `pixelsPerPoint`, `inputNotice`, `launch`, and optionally `continuousTouch` |
| `boot` | `device` | The status once the device has booted. 120-second limit |
| `shutdown` | `device` | The status after shutdown. 120-second limit |
| `screenshot` | `device`, optional `format`: `png` (the default) or `jpeg` | A [frame](#frames-and-coordinates) |
| `tap` | `device`, `generation`, `width`, `height`, `x`, `y` | `{sent:true}` |
| `swipe` | The `tap` fields, plus `endX`, `endY`, `durationMs` (50 to 5,000) | `{sent:true}` |
| `text` | `device`, `text`: 1 to 4,096 UTF-16 code units, with no control characters or unpaired surrogates | `{sent:true}` |
| `button` | `device`, `button`: `home` or `lock` | `{sent:true}` |
| `rotate` | `device`, `orientation`: `portrait`, `portrait_upside_down`, `landscape_left` or `landscape_right` | `{sent:true}`. Invalidates the geometry even if the input fails |
| `install` | `device`, `path`: an absolute path to an existing `.app` folder | `{installed:true}`. 120-second limit |
| `launch` | `device`, `bundle`, optional `sources` | The console's status, plus `state:"starting"` |
| `terminate` | `device`, `bundle` | `{terminated:true}`. Closes the console and ends the app. An app that already ended isn't an error |
| `log` | `cursor`: an integer, 0 at first | `{page:{entries,nextCursor,truncated},launch}` |
| `report` | `device`, `bundle` | `{text}`: the file `Documents/as3test.txt` in the app's data container, at most 1 MiB |

Notes on single methods:

- **`launch`** acknowledges that the process was created, not that the app started. `sources` is the
  absolute folder where the game's scripts are edited. The service gives it to the app as
  `KAS3_SOURCE_DIR`, so that the app reloads scripts when you save.
- **`report`** fails with `DEVICE_CHANGED` if the selection changes while it reads, and with `STALE_REPORT`
  if the session changes and returns to the same device. A missing report is an error, not an empty
  report.
- **`sent:true`** means the input backend acknowledged the command, not that the app reached a state you
  expect. Check the next screenshot or the log.

Other Xcode commands have a 15-second limit. A client should wait up to 130 seconds for a reply. A timeout
doesn't prove that an operation did nothing. Request ids match replies to requests. They don't deduplicate
a retry.

### Errors

`BAD_REQUEST`, `NO_DEVICE`, `DEVICE_CHANGED`, `DEVICE_UNAVAILABLE`, `STALE_FRAME`, `STALE_REPORT`,
`INPUT_DISABLED`, `INPUT_UNAVAILABLE`, `BUSY`, `PROCESS_FAILED`, `TIMEOUT`, `CLOSED` and `INTERNAL_ERROR`.

Input can't be enabled through the protocol. The user switches it on in the panel, or starts the
standalone service with `KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1`.

## Frames and coordinates

A frame has `device`, `width`, `height`, `generation`, `format`, `data`, `capturedAt` and `captureMs`.

- `data` is the image, base64-encoded.
- `capturedAt` is the host's time in milliseconds since the Unix epoch, sampled after the capture.
- `captureMs` is how long the capture and the image processing took. It isn't input lag.

Coordinates are finite numbers in image pixels, from the top left of the image: `0 <= x < width` and
`0 <= y < height`.

An input request copies `width`, `height` and `generation` from a frame. The service checks them against
the geometry it last knew. A new selection, a failed capture, a rotation or a change of size invalidates
old geometry, and input that quotes it is refused with `STALE_FRAME`. A change made outside the service is
noticed on the next capture, so take a fresh screenshot before a gesture.

The service converts screenshot pixels to device points by the device's pixel density. There's one display
and one pointer gesture at a time.

## Logs

Each log entry has `cursor`, `stream` and `text`. A stream is `stdout`, `stderr` or `session`.

- Entries are chunks of text, not necessarily whole lines.
- `nextCursor` is the value to send on the next call. The cursor never resets during a session, and a
  client must not send a cursor from the future.
- `truncated:true` means some of the history asked for was discarded by the memory limit.
- Output from earlier launches stays in the buffer, with a `session` entry that names each launch.

The streams are the pipes of the console launcher, and Xcode can combine the app's output into one. The
`launch` status has `bundle`, `running` and, after the process exits, `exitCode`. They describe the
launcher. Read the app's output and its test report before you conclude that the app started or passed.

## Shutdown

Closing the IDE project or the standalone service removes the endpoint file and the temporary files, stops
the HTTP listener, and stops the console process and every helper the service started. `simctl` forwards
signals, so this can stop the launched app. The Simulator stays booted.

After a crash, an old endpoint file can remain. It isn't a live session, and a client must not reuse it.

## The native helper's transport, version 1

This transport is between the service and the Swift helper. It's an implementation detail, separate from
the HTTP protocol above. The helper runs only when the user has switched input on. Without it, pictures are
`simctl` screenshots.

`continuousTouch` in the status is true only while the helper runs. `pixelsPerPoint` is the last known
density, and is 1 until a picture or an input discovers it.

### Startup

The service starts `simulator-bridge --device <id> --frames <new absolute file>`. It owns the helper's
standard input and the temporary folder.

1. The helper creates the frame file with mode 0600. It refuses an existing file or a symbolic link.
2. It connects to the selected device and publishes a first frame.
3. It writes one JSON line with `ready`, `width`, `height` and `density`.

If startup fails, the helper writes a line with `error` and `code` and exits with a nonzero status.
`DEVICE_UNAVAILABLE` means the device is stopped or restarting, or its input service isn't ready. The
service keeps the native route and tries again later. Any other failure is reported to the panel.

Closing the helper's standard input cancels input, even while a command is in flight. The helper then
releases held controls, unmaps the frame file and releases the device. The service waits for the helper to
exit before it deletes the files.

### Control lines

Each line is a JSON object with a positive `id` and an `op`:

| `op` | Fields |
|---|---|
| `ping` | None |
| `down`, `move`, `up` | `x` and `y`, finite, in device points |
| `home`, `lock` | None |
| `rotate` | `orientation`, with the same four names as the HTTP protocol |
| `text` | 1 to 4,096 printable ASCII characters |
| `paste` | None. Sends Command-V to the device |

A reply has `id`, `ok`, `receivedNanos`, `completedNanos`, and `error` on failure. The service waits for
each reply before it sends the next command.

- **Text that isn't ASCII** is copied to the device's pasteboard with `simctl pbcopy`, followed by `paste`.
  The host's pasteboard is never written. iOS can show an Allow Paste prompt, which the user accepts in
  the picture. An app can refuse a paste. A successful reply confirms the transport, not that text was
  inserted.
- **Touch.** The panel sends `down` and `up` in order and replaces unsent `move`s with the newest point.
  Losing focus, hiding the panel and closing it release the gesture.
- **Input whose delivery is unknown is never sent again.**
- **Rotation** ends the frame feed. The service opens a new one, which reads the new orientation. A
  rotation made outside the service ends the feed too, so old coordinates can't reach a rotated screen.

### The frame file

Frames never travel as JSON or base64. The helper writes each one into a memory-mapped file that the
service reads. Both sides read the layout from
[`frame-layout.json`](native/SimulatorBridge/Sources/SimulatorBridge/Resources/frame-layout.json) and test
against the same golden header. The table documents that file. It doesn't define a second layout.

Version 1 is little endian: a 64-byte header, then rows of premultiplied BGRA pixels.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | Magic, 827151179. Zero while unpublished |
| 4 | 4 | Version, 1 |
| 8 | 8 | Sequence, which only increases |
| 16 | 4 | Width |
| 20 | 4 | Height |
| 24 | 4 | Stride in bytes, padding included |
| 28 | 4 | Pixel format, 1111970369 (`BGRA`) |
| 32 | 8 | When this changed surface was first observed, in monotonic nanoseconds |
| 40 | 8 | When it was written, in monotonic nanoseconds |
| 48 | 4 | Native width |
| 52 | 4 | Native height |
| 56 | 4 | Density, in thousandths of a pixel per point |
| 60 | 4 | Flags, 1 for valid |

Limits: 64 MiB of pixels, a width and a height of at most 4,096, a stride of at most 16,384, and a density
of 1,000 to 10,000.

**Writing.** The helper takes a nonblocking lock on the whole file, clears the header, copies the pixels,
and writes the complete header last. If the lock is held, it drops that frame.

**Reading.** The service reads the header under a shared lock, allocates the image after releasing the
lock, then locks again and checks the header before it copies. It copies only a new sequence, off the UI
thread, into a slot that holds the newest frame. A zero header is skipped. There's no queue.

### What the timestamps mean

- The observation time is when the helper noticed a changed surface. It isn't the time the Simulator
  presented the frame.
- A reply to an input command follows a separate barrier message. It shows that the device's input service
  received the input in order. It doesn't show that the app handled it or that the screen changed.
- `receivedNanos` is when the helper processed the command, not when the device received it.
- A ping at startup estimates the offset between the helper's clock and the service's.
