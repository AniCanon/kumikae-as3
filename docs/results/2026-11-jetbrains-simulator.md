# JetBrains Simulator checks

Date: 2026-11-14. Work on `feat/jetbrains-simulator`.

The [design](../superpowers/specs/2026-11-14-jetbrains-ios-simulator-design.md)
was committed before the implementation. Code and build instructions are in
[`editors/jetbrains/`](../../editors/jetbrains/).

## Earlier snapshot verification

For the current live-picture implementation, see
[the speed measurements below](#live-picture-and-input-measurements-2026-11-14).

The first pass below was written when the build could not resolve one
dependency, so nothing had compiled. That is no longer true. A second pass on
the same day made the build work, checked the service against a real Simulator
and then rebuilt the panel. No person has taken part in any check: everything
here was run by the agent maintaining this branch. The owner has not yet opened
the panel in Rider.

| Check | Who and how | Observed result |
|---|---|---|
| Missing dependency | Downloaded with the owner's permission | `error_prone_annotations` 2.41.0 resolved; `:core:test buildPlugin` succeeded for the first time. |
| Service against a real Simulator, before the rework | Standalone service and `simulator.py`, by hand | Devices listed; a booted iPhone on iOS 26.4 selected; `org.kumikae.as3test` (the Godot test app) launched; PNG screenshot 2622x1206, captured in 435 ms; console log read; test report read: "14 passed, 0 failed". |
| Rework onto the owner's JetBrains plugin stack | This branch | Gradle wrapper 9.8.1, Kotlin 2.4.10 with Compose, ktlint 1.8.0, Compose Swing UI 0.1.0-alpha06, Java 25. Packages moved to `app.anicanon.kumikae.as3`. The single Swing class became a repository, a controller, a composed view and a composition root. |
| `./gradlew --offline check buildPlugin` | Gradle 9.8.1 from the wrapper, JDK 25, normal Gradle home | Succeeded. Style check clean in both modules. 18 service tests and 34 panel tests ran, none failed or skipped. Plugin zip produced. Every run was offline. The one artifact the normal Gradle home lacks, `error_prone_annotations` 2.41.0, was read from the earlier project-local cache by setting `GRADLE_RO_DEP_CACHE` to `.gradle-home/caches`. A plain run on this Mac therefore still has to fetch that one artifact into the normal home; that has not been done. |
| `./gradlew --offline verifyPlugin` | JetBrains' plugin verifier, already cached | Compatible with RD-262.10968.170. Its deprecated and experimental notes all concern members that Kotlin copies from the `ToolWindowFactory` interface, not calls made by this code. |
| Tests written first | This branch | The 10 repository tests, 20 controller tests and 3 of the 4 service-adapter tests were each seen failing before the code existed, then passing. |
| `python3 -m unittest discover -s tests` | Python | 7 passed. The wire protocol did not change. |
| Service against the same Simulator, after the rework | Rebuilt standalone service on Java 25 and `simulator.py`, by hand | Same sequence again: devices, select, launch of `org.kumikae.as3test`, PNG 2622x1206 captured in 565 ms, console log, report "14 passed, 0 failed". The service was stopped afterwards; the Simulator was left booted and nothing was installed on it. |
| The panel's own repository against the same Simulator | One throwaway test over `LocalSimulatorService` and `ApiSimulatorRepository`, not kept | Select, a JPEG picture decoded to 1206x2622 with a 403 ms capture, an empty console page and status all worked. Read-only: nothing launched or installed. |
| `./gradlew --offline runIde -PuiProbe` | Sandbox Rider 262.10968.170, synthetic service, no window shown | `passed`. The production view was mounted in the light and dark themes; Refresh, Boot, Launch and a failing Install were pressed through the real buttons; a device picked during a running boot was refused and the list returned to the booted device; input controls followed the service's permission; closing released the composition and closed the service. Eight pictures were written and looked at. Two layout faults were found and fixed: the device list was cut to a few characters at tool-window width, and the Home and Lock buttons were out of line. One is left: in a wide, short window the form takes most of the height and the picture is thumbnail-sized. |

Still not verified:

- **The panel in a real Rider window, used by a person.** The probe draws the
  panel off screen with a made-up service. Nobody has resized it, hidden it,
  watched it follow a live Simulator or closed a project with it open.
- **The panel's controller and view against a real Simulator.** The repository
  under them was run once against the booted device. Polling, the picture on
  screen and the console inside the window have only been seen with the
  synthetic service.
- **idb input**: taps, swipes, text, buttons and rotation. idb is not installed.
- Any other JetBrains IDE, any other platform build, and the absence of the
  tool window on Windows and Linux.
- A cold boot from the panel, and installing an app from the panel.

The sections below are the first pass, kept as the record of what was measured
then. Where they say the build is blocked or the Kotlin tests have not run, the
table above replaces them.

## What was run in the first pass

| Check | Observed result |
|---|---|
| `xcodebuild -version` | Xcode 27.0, build 27A266a. |
| `simctl list -j devices available` | One booted iPhone 17 Pro on iOS 26.4. No device in the iOS 27.0 list. Identifiers and the local device name are omitted here. |
| `simctl help`, `help io`, `help launch`, `help bootstatus` | Read the installed commands and flags. No tap or swipe command. |
| Ten PNG and ten JPEG captures to files | All succeeded. A JPEG was opened and the home screen was visible. Times below. |
| `simctl io <device> screenshot --type=png -` | Exit 0, zero stdout bytes. Wrote a file named `-`, despite help advertising stdout. That temporary file was removed. |
| `simctl io <device> recordVideo --force --codec=h264 /dev/stdout` with a pipe | Zero video bytes, AVFoundation error -11823, “Cannot Save”. Without `--force`, it failed because the destination already existed. |
| `simctl bootstatus <device> -b` | Exit 0 for the already booted device. This was not a cold-boot test. |
| `simctl launch --terminate-running-process --console <device> com.apple.Preferences` | Started Settings. Received 29 bytes on stdout and 5,943 on stderr. The console process exited 0 after it was sent SIGTERM. The output was not retained. |
| A JPEG after launching Settings | Capture exited 0. The image was opened and Settings was visible. The image was then removed. |
| Computer Use access to Device Hub | Refused by the tool. No host-window input or Accessibility geometry inspection followed. |
| Python CLI tests | Seven tests failed against stubs, then all seven passed against a fake local HTTP service. |
| `./gradle-offline :core:test buildPlugin --continue` | Failed before Kotlin compilation. Missing `com.google.errorprone:error_prone_annotations:2.41.0`, required by Gson 2.13.2. No plugin zip produced. |
| `./gradle-offline :core:dependencies --configuration testRuntimeClasspath` | Report completed. That artifact was the only unresolved entry in the core test runtime graph. A successful report is not a successful build. |
| XML parsing and `git diff --check` | Plugin descriptor was well-formed. No whitespace errors. |

Gradle and CoreSimulator needed unsandboxed processes for local sockets. All build
outputs and writable Gradle caches were directed into `editors/jetbrains/`.
No dependency, SDK, runtime or idb package was downloaded. Existing Gradle caches
were read, not changed. Nothing was pushed or merged.

## Capture measurements

Each value is elapsed wall time from starting `xcrun` until it exited. Captures
were sequential, on the booted home screen. Files were overwritten between calls.
This measures the command path, not IDE painting or input-to-picture delay.

| Format | Samples, milliseconds | Median | Range | Final image size |
|---|---|---|---|---|
| PNG | 638.5, 674.6, 636.6, 628.2, 609.4, 575.7, 597.8, 631.6, 630.2, 731.3 | 630.9 ms | 575.7–731.3 ms | 2,887,404 bytes |
| JPEG | 554.9, 594.2, 521.7, 532.6, 514.0, 502.2, 526.4, 512.0, 508.8, 861.7 | 524.05 ms | 502.2–861.7 ms | 462,110 bytes |

These results justify a snapshot viewer, not a claim of smooth video. End-to-end
frame rate and delay inside Rider are unmeasured.

## What had not been verified after the first pass

- Kotlin compilation and the 18 Kotlin tests. The first core tests were written
  against stubs, but dependency resolution failed before an assertion could run.
  Further tests added during source review also remain unrun. The Python client
  tests did complete a failing-then-passing cycle.
- The plugin in Rider, its layout, packaging, disposal and the absence of its
  panel on Windows and Linux. Those are implemented behaviours, not observations.
- The Kotlin HTTP server and standalone distribution end to end. The Python
  tests used a fake service, not a substitute build of the Kotlin code.
- idb input, display scale, companion lifecycle, Xcode 27 compatibility and input
  delay. idb was not installed. The adapter is opt-in and was not enabled.
- Install or launch of `kas3test.app`, AS3 logs, or the test report path in its
  Simulator container. The custom template was separate work and was not touched.
- Capture of the host window, ScreenCaptureKit, direct private-framework access,
  XCUITest input, or any other live-video transport. Only their documentation was
  compared. The failed stdout pipe test does not rule out every possible URL.

## Next verification

Start the sandbox Rider with `./gradlew runIde` and use the panel against the
booted Simulator: the home screen, Settings and the Godot test app, including
resizing, hidden-panel polling, agent and IDE device changes, errors and
closing the project.

Then decide whether to adopt idb's private interfaces. A compatible client and
companion must be selected, installed and tried together. Check tap targets at
each edge, a swipe, text, home, lock and both landscape rotations. Check that the
agent and panel operate the same device. Until then, use Device Hub for input.

The Godot Simulator export has been launched and its report read through the
service. Installing it from the panel is still to be tried.
This work does not complete the roadmap's AS3 debugger gate.

## idb input, 2026-11-14

Installed with the owner's agreement: idb's companion 1.7.8 from Meta's Homebrew tap (that one
formula marked trusted) and the `fb-idb` client in its own environment. Xcode 27.0, a booted
iPhone-class Simulator on iOS 26.4.

| Check | Observed |
|---|---|
| `idb describe --json` | Density 3.0, 402 by 874 points for a 1206 by 2622 screenshot. |
| The standalone service with only `KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1` | Status: input enabled, 3.0 pixels per point, found without a path or a scale being given. |
| `simulator.py button home`, then `tap` at screenshot pixel 1017, 1302 (the Settings icon) | Both accepted. idb's accessibility listing then began with "Settings": the tap landed where the picture showed the icon. |
| `text`, `swipe`, `button lock`, `rotate landscape_left`, `rotate portrait` | All accepted. Their effect on screen was not separately confirmed. |

Not run: input from the panel's own buttons and mouse in a Rider window; a device idb cannot
describe; a second device; input delay.

## Install idb from the panel, 2026-11-14

| Check | Observed |
|---|---|
| The install script run with the bare environment an IDE gives a process | Exit 0 on a Mac that already had idb: Homebrew and the client each reported it installed. A first install from nothing was done by hand earlier the same day with the same commands, not through this script. |
| The script inside the built plugin | Present in the plugin's jar; a test reads it back. |
| The panel's flow (ask, run, show output, switch input on; failure; retry) | Controller tests with a stand-in installer, and installer tests with small scripts. Not pressed in a Rider window. |

## Live picture and input measurements, 2026-11-14

Branch: `feat/simulator-speed`. This section supersedes the snapshot performance and
unverified-input statements above. The owner had tried the snapshot panel in Rider
and reported that it was slow. The measurements below were automated on this Mac,
against the already booted **Glass Dock Demo**, using Home and Settings only. No
Simulator was created, shut down or erased; no app was installed. Xcode 27.0,
iOS 26.4, idb companion 1.7.8, native picture 1206 by 2622, density 3.

### Before and after

| Measurement | Observed |
|---|---|
| Existing screenshot command, six sequential JPEG captures | 1383.81 ms first capture; then 395.05, 458.89, 409.91, 457.84, 379.08 ms. This is capture-command time, not painting. The old panel requested at most two pictures per second. |
| Existing per-event Python input, twelve taps | 340.86–361.36 ms from process start to completion. |
| Raw dynamic MJPEG, quality 0.6, scale 0.33 | 57.73 frames/s during Settings swipes; first complete JPEG after 401.34 ms; mean JPEG 41,923 bytes. |
| Raw MJPEG with `--fps 30` | 30.04 frames/s while changing, but 113 frames in the first four seconds on a still screen. Dynamic mode was selected to avoid forced duplicate pictures. |
| **Actual panel painting**, visible 420 by 900 sandbox window | **25.30 distinct pictures/s** during sixteen alternating Settings swipes. The source pictures were 398 by 866. |
| JPEG fully received to actual panel paint | Median **21.50 ms**, p95 **29.92 ms**. This includes decode, newest-frame delivery and painting; it excludes Simulator capture/encoding before receipt. |
| Actual panel mouse release to acknowledged warm tap | Twelve samples: 95.17, 98.16, 94.55, 90.21, 92.71, 95.69, 94.16, 99.01, 100.98, 103.86, 88.33, 89.39 ms. Acknowledgment follows delivery, so this is an upper bound on handoff latency. |
| Still and hidden panel | Zero new paints during five seconds after Home settled; zero during 1.5 seconds after hiding. The dynamic stream waits for changes; the empty newest-frame timer does not decode or repaint. Visible metadata still polls every 500 ms. |
| Settings launch request to next paint | 246.96 ms. The next paint is not proven to be a Settings frame; this is not an end-to-end display-latency claim. |
| Cold private-companion input | First tap took 574 ms to acknowledge. Companion logs placed touch-down about **489 ms** after request; the next tap reached touch-down in about **2 ms** and acknowledged in 91 ms. **The first-tap target is not met.** |

The panel uses the same in-process session as the authenticated HTTP agent service.
Binary pictures avoid base64 JSON, including in screenshot fallback. HTTP commands
and the Python client's protocol remain unchanged. idb runs only after the existing
input switch. A private companion and persistent `idb shell --no-prompt` avoid Python
startup per gesture and avoid attaching to the owner's existing companion. No new
dependency, codec, Python helper or download was added.

The live source omits `--fps`, uses quality 0.6 and scale 0.33, and keeps one newest
encoded picture and one newest decoded picture. The UI samples at 33 ms. Stream
failures revoke geometry and retry after 0.5, 1, 2, 4 and at most 8 seconds. Input
acknowledgments and screenshots run outside the session monitor.

### Ten-minute streaming run

A temporary Java harness called the production Kotlin `LocalSession`, consumed its
newest decoded frame every 33 ms, and sent alternating 700 ms Settings swipes with
100 ms between them. It ran for 610 seconds with `-Xmx256m`. This measures the service
and decoding path; it does not measure ten minutes of Rider's entire heap. The
separate sandbox probe above measured actual panel painting. The service hot path
was the same; later changes strengthened close/error invalidation and cleanup.

Across sixty complete ten-second windows: **15,881 frames**, mean **26.42 frames/s**,
range **24.07–27.53**. Per-window receipt-to-consume p95 was 18.26–30.85 ms;
decode p95 was 2.28–8.93 ms. A forced GC once a minute measured retained JVM heap:
5.655, 5.864, 5.652, 5.650, 5.517, 6.527, 5.493, 5.496, 5.709, **5.706 MiB**.
There was no monotonic retained-heap growth. Allocation between collections is
expected and was not counted as retained growth.

`ps -axo pid,ppid,pcpu,rss,time,command` sampled the harness and its owned descendants
every ten seconds. CPU below is cumulative process CPU time divided by elapsed wall
time, where 100% means one core. RSS is resident memory, in MiB.

| Process | Mean CPU | First RSS | Last RSS | Peak RSS |
|---|---:|---:|---:|---:|
| JVM service and decoder harness | 17.78% | 222.41 | 227.55 | 334.94 |
| Owned idb companion | 9.16% | 60.73 | 61.64 | 61.69 |
| Persistent idb input shell | 0.09% | 54.16 | 54.28 | 54.33 |
| idb video-stream client | 2.49% | 55.20 | 55.34 | 55.39 |

The JVM's retained heap returned close to its initial value; resident memory ended
5.14 MiB above the first sample after a higher temporary peak. These observations
support bounded memory in this run, not a claim of byte-for-byte constant RSS.
After closing the harness, `pgrep -lf 'idb|SimulatorSpeed|kumikae-simulator'` showed
only the companion that existed before this task. It was left untouched.

A final 25-second Home run on the rebuilt distribution consumed 3.60 and 3.99
frames/s in its complete ten-second windows. This screen was not completely still:
a subsequent 15-second pixel-comparison probe found differences within the top 65
rows of the 398-by-866 pictures, near the status bar. It did not find identical
decoded pictures being repainted. Over seconds 5–20 of the uninstrumented Home run,
cumulative CPU cost was 2.4% for the service, 0.8% for the companion, 0% for the input
shell and 0.33% for video. Thus the earlier five-second zero-paint observation does
not imply that Home always stops producing pictures.

### Commands and verification

All Gradle commands used the existing JDK at `/opt/homebrew/opt/openjdk@25` and
`GRADLE_RO_DEP_CACHE=<cache-worktree>/editors/jetbrains/.gradle-home/caches`.
`<worktree>` is this branch's worktree; `<id>` replaces the booted device identifier.
Nothing was downloaded. Local socket/process checks needed execution outside the
filesystem sandbox.

```sh
cd <worktree>/editors/jetbrains
./gradlew --offline check buildPlugin verifyPlugin
./gradlew --offline -q :core:installDist
KUMIKAE_SIMULATOR_LIVE_PROBE=1 ./gradlew --offline runIde -PuiProbe
./gradlew --offline runIde -PuiProbe
python3 -m unittest discover -s tests
```

| Check | Result read from the output |
|---|---|
| Full offline check, build and verification | `BUILD SUCCESSFUL`; 46 core and 75 panel tests, zero failures, errors or skips. Compatible with RD-262.10968.170; four deprecated and six experimental `ToolWindowFactory` API notes remain. |
| Ordinary UI probe | `BUILD SUCCESSFUL`; `build/reports/ui/result.txt` contained `passed`. Representative narrow, wide and error-state PNGs were inspected in light and dark. |
| Opt-in live UI probe | `BUILD SUCCESSFUL in 52s`; `passed`, with the painting and input results above. |
| Python client suite | Final run: `Ran 7 tests in 3.529s`, `OK`. The first sandboxed attempt could not bind its loopback test server; the allowed unsandboxed rerun passed. |
| Regression cycles | Parsing/shell APIs initially failed compilation before implementation. Concurrency, closed-shell reuse and oversized dimensions produced three failing tests before fixes. Closing a live feed produced one failing test before geometry revocation. A forked JVM left its owned child alive before the shutdown-hook fix. Some controller tests were added alongside implementation, so a failing-first run is not claimed for every test. |
| Independent review | Reviewed lifecycle, geometry, input concurrency and process cleanup. Findings were addressed; the final close-time binding fix was reviewed again. |

The process owner registers a JVM shutdown hook when it first starts a child and
unregisters it after normal close. This prevents IDE exit from cutting short the
panel's asynchronous cleanup. A forked-JVM test checks this with an owned process
that ignores graceful termination. Project-close cleanup in the owner's Rider is
still not a manual observation.

The opt-in live probe runs the ordinary synthetic light/dark probe first, then mounts
the production controller, repository and screen in a visible sandbox JFrame. It
uses real mouse press/release events and counts distinct frames in `paintComponent`.
Its sources are `UiProbeLive.kt` and `UiProbeStarter.kt`; reports are generated under
`build/reports/ui/`. It closes its own window and session, not the owner's Rider.

The temporary command probes and harness were run as follows (temporary source and
logs were not committed):

```sh
python3 -I /tmp/kumikae-speed-baseline.py
python3 -I /tmp/kumikae-shell-probe.py
python3 -I /tmp/kumikae-private-warm.py
python3 -I /tmp/kumikae-hid-handoff.py
javac -cp 'core/build/install/kumikae-simulator/lib/*' /tmp/SimulatorSpeed.java
KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1 java -Xmx256m \
  -cp '/tmp:core/build/install/kumikae-simulator/lib/*' SimulatorSpeed 610 swipes
python3 -I /tmp/kumikae-monitor.py
KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1 java -Xmx256m \
  -cp '/tmp:core/build/install/kumikae-simulator/lib/*' SimulatorSpeed 25
javac -cp 'core/build/install/kumikae-simulator/lib/*' /tmp/SimulatorStill.java
KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1 java -Xmx256m \
  -cp '/tmp:core/build/install/kumikae-simulator/lib/*' SimulatorStill 15
```

`SimulatorStill` was a temporary copy of the harness with per-pixel comparison
between consecutive decoded pictures. Its timing includes that comparison and is
not used as a production latency measurement. A final `simctl` JPEG was inspected
to locate the changing region; temporary pictures were not committed.

The baseline scripts ran `xcrun simctl io <id> screenshot --type=jpeg <temporary-file>`,
`idb ui tap --udid <id> <x> <y>`, and `idb video-stream --udid <id> --format mjpeg
--compression-quality 0.6 --scale-factor 0.33`, with and without `--fps 30`.
The private-companion probes started a companion with `--udid <id> --grpc-domain-sock
<private-directory>/companion.sock --only simulator`, then `idb --companion <socket>
shell --no-prompt`. Empty text returned `SUCCESS=0` and did not initialize touch.
Every probe stopped the children it started.

### Limits and decisions still open

- Cold first-touch delivery exceeds 150 ms. A helper using the already installed
  Python client to initialize HID without a touch was proposed to the owner; it is
  not included. Starting a connection is not sufficient to initialize HID.
- Receipt-to-paint timing excludes source capture and encoding. No synchronized
  source timestamp or optical end-to-end measurement was available.
- The ten-minute memory run covers streaming and input in a standalone harness,
  not the entire Rider process. Manual acceptance of the live panel, project-close
  behavior in the owner's Rider, other devices and other IDE builds remain unverified.
- Swipes still dispatch on mouse release; there is no continuous finger tracking.
- Fixed quality 0.6, scale 0.33, a 33 ms UI timer, a private companion per session,
  and the in-process-only live transport are deliberate choices the owner may change.
- Existing faults found and fixed: screenshot capture held the session monitor;
  input acknowledgment held it during swipes; dimension narrowing could wrap a
  large integer. Review of the new streaming implementation also caught failed
  shell reuse, a closed stream appearing healthy, late geometry binding after
  close, and cleanup that did not await children at service or JVM exit. These
  findings were fixed before the final build.

## Native framebuffer and input measurements — 2026-11-14

Work on `feat/simulator-native`, in its own worktree. The installed tools reported
Xcode 27.0 build 27A266a, Swift 6.4, macOS 27.2.1, iOS 26.4 and JDK 25. No downloads,
app installs, simulator creation, erasure, intentional shutdown or reboot were
performed. Only the already booted demo device's Home and Settings were driven.
The helper and all sandbox probes owned their own processes; other tools' processes
and the owner's windows were left alone.

### Decision and the rejected routes

Choose the CoreSimulator main-display IOSurface and a single mapped BGRA frame
slot. The surface was **1206×2622**, stride **4864**, payload **12,753,408 bytes**.
A read-only lock/copy/unlock probe recorded 39 copies: most were 0.2–0.3 ms, maximum
2.63 ms. That is copying cost, not presentation latency. The final helper polls
seeds every 8 ms and skips unchanged or concurrently changing surfaces. It registers
no private display callbacks and captures no window chrome.

Screen Recording preflight (`CGPreflightScreenCaptureAccess`) returned false.
ScreenCaptureKit was therefore unavailable without asking for another permission;
no permission prompt or timing run was attempted. It also needs a visible source
window. There is **no measured ScreenCaptureKit performance comparison**, and
headless operation of the chosen route was not separately tested by closing the
owner's windows.

A Python transport comparison used identical 12.75 MB payloads, ten warmups and
100 measured request/write/read cycles. Pipe median/p95 were **1.630/1.977 ms**;
mapped-slot median/p95 were **0.470/0.589 ms**. The mapping used record locks. These
numbers exclude Simulator capture, Kotlin conversion and painting; they motivated
the mapped transport, not an end-to-end latency claim.

Input uses the guest **DTUHID XPC service**, not the older legacy HID client. The
legacy path acknowledged sends but a 40-move Settings drag produced zero changed
frames and an unchanged screenshot. An initial live-probe run on that broken path
reported only 5.53 painted frames/s. Its fast acknowledgments were not evidence of
delivered input. Read-only inspection of installed Apple receiver code established
the new message schema. With DTUHID, the same gesture changed 79 frames in 1.323 s,
and a screenshot visibly confirmed scrolling. Home was confirmed; native text
`Display` visibly appeared in Settings search. Lock was not visually checked.

**Exploration incident:** an early private screen-callback probe with nil optional
callbacks coincided with two SimRenderServer crashes and dependent backboardd and
test-app failures. The server fault dereferenced a null callback, making the probe
the likely cause; causation was not completely established. The owner was informed,
live probing stopped while the ABI was inspected, and that registration path was
removed from the implementation. The helper uses seed polling only. No deliberate
Simulator restart was used to recover or test it.

### Actual panel, short run before the decoration cache

The production controller/repository/screen ran in a visible sandbox Rider. Real
mouse press/release events exercised touch, then sixteen alternating 700 ms
Settings swipes drove animation. Distinct new frames were counted inside
`paintComponent`. This measures paint callbacks, not physical monitor scanout.

| Metric | Earlier idb panel | Native panel, 13.58 s animation window |
|---|---:|---:|
| Distinct frames painted per second | 25.30 | **74.88** |
| Delivered resolution | 398×866 | **1206×2622** |
| Measured pipeline delay, p50 | JPEG receipt → paint: 21.50 ms | Surface observation → paint: **18.13 ms** |
| Measured pipeline delay, p95 | JPEG receipt → paint: 29.92 ms | Surface observation → paint: **23.38 ms** |
| Warm pointer acknowledgment | Release → acknowledgment: 88–104 ms | Press → up/barrier acknowledgment: **2.97–6.71 ms** |
| First gesture after the picture was ready | About 489 ms | **17.22 ms** |
| New paints during five seconds still / 1.5 seconds hidden | 0 / 0 | **0 / 0** |

The timing origins differ between routes. Native observation is the first seed
poll that notices a changed surface, before copying. Swift/JVM monotonic clocks
are aligned by a startup ping midpoint; half its round-trip is the uncertainty
bound, which this report did not record separately. Observation excludes up to a
poll interval and any earlier Simulator work. **Under 50 ms from actual Simulator
presentation to panel paint is not verified.** The launch-request-to-next-paint
measurement was 180.38 ms and is not an application launch-completion measurement.

The twelve native gesture acknowledgment values were 17.218, 3.051, 5.229, 5.460,
6.171, 6.207, 2.974, 5.510, 4.294, 6.706, 5.401 and 3.484 ms. Each acknowledgment
follows a separate DTUHID barrier reply, establishing receive-queue progress in
the guest input service. It does not prove application handling or visual response.
Helper startup precedes frame readiness and is excluded from this first-gesture
number. A fresh helper's seven-character text command acknowledged in 2.013 ms;
the resulting Settings search was inspected separately.

The EDT consumed **6.174 CPU seconds in 13.582 wall seconds**, or **45.45% of one
core**. This includes all EDT work, not just `drawImage`; painting CPU was not
isolated from layout/event processing. The short run alone does not establish a
sustained frame rate on a busy shared Mac.

### Ten-minute helper run

A Python harness launched Settings publicly, opened one release helper and sent
alternating 40-move drags (17 ms between moves, 150 ms between gestures) for at least
600 seconds. It sampled `ps` every thirty seconds. **24,024 input phases** completed;
acknowledgment p95 was **4.376 ms**, maximum **82.000 ms**. Thus the 30 ms target was
not met for every event under this workload. EOF produced exit **0**, empty stderr,
and no surviving owned helper.

From the first to last sample (570.54 s), helper CPU was **21.02% of one core**.
RSS started at **53.016 MiB**, peaked at **53.688 MiB**, and ended at **48.453 MiB**;
the last several samples stayed there. There was no monotonic RSS growth. This
harness did not map/copy pixels in a JVM. Its unlocked sequence sampling sometimes
saw the deliberately invalidated zero header; those samples are not frame counts.
Kotlin's production reader takes the record lock. Some builds overlapped the run.

### Ten-minute visible panel run

The first completed visible-panel run lasted **600.74 s** before the static device
body was cached. It painted a weighted mean **30.86 frames/s**; one-minute windows
ranged from **23.46 to 42.78 frames/s**, with observation-to-paint p95 between
**51.02 and 70.15 ms**. Its preceding short run was 37.74 frames/s, with the first
two gesture acknowledgments at 62.46 and 32.09 ms. The faster short run above was
therefore not representative of every run.

| Sampled quantity | Start | Minimum | Maximum | End |
|---|---:|---:|---:|---:|
| Helper RSS, MiB | 53.97 | 37.91 | 53.98 | 37.91 |
| IDE RSS, MiB | 2677.36 | 1493.73 | 2677.36 | 2262.72 |
| JVM used heap after requested GC, MiB | 163.78 | 143.34 | 210.03 | 210.03 |

Mean CPU over those samples was **27.29% of one core for the helper**, **45.48%
for the EDT**, and **92.84% for the whole IDE**. These are CPU-time deltas divided
by elapsed time; EDT time includes event processing and layout, not only painting.
There was no monotonic helper RSS increase. IDE samples fluctuated; the higher last
heap sample means this run alone does **not prove an absence of IDE memory growth**.

The test-only extension samples JVM heap after requesting GC once per minute,
IDE/helper RSS and cumulative CPU from `ps`, and EDT CPU from the thread management
bean. It clears numeric per-frame timing samples each minute. GC and measurement
work are included in the windows. Numerous unrelated CPU-heavy test workers were
running on this shared Mac; they were left alone. A 30-second JFR profile and a
small offline build also overlapped part of this first run. It is not an isolated
benchmark.

Earlier endurance attempts exposed an unsupported `ProcessHandle` CPU counter,
the existing two-minute probe timeout, and an IDE evaluation-feedback modal that
blocked default-modality test dispatch. The probe now reads cumulative CPU from
`ps`, permits twelve minutes only for the explicit endurance flag, and dispatches
its own test actions with `ModalityState.any()`. The stalled owned probe IDE was
stopped and its shutdown hook reaped its helper. The owner's IDE was untouched.

The JFR profile sampled mostly Java2D shape rasterization on the EDT. The screen
redrew its unchanged shadows and bezel for every frame. A Retina-density cache
now renders that decoration only when geometry or display density changes; a
pixel-equivalence, reuse and invalidation test failed before the cache was
implemented. Independent review caught an unbounded cache allocation at high zoom
on large Retina views. A second failing-first test now checks the 16 MiB cap and
direct-paint fallback. The focused re-review reported no remaining findings.

The first short cached-body run painted **47.51 frames/s**, with
observation-to-paint p50/p95 **29.62/42.78 ms**, and **54.50% of one core** on the
EDT. A real Swing drag painted **52 frames before release**, demonstrating live
tracking rather than a gesture sent only on release. Both zero idle paints and
zero hidden paints were retained. Conditions differed between runs, so this is
not a controlled attribution of speedup solely to the cache.

The final cached-body run completed **600.41 s** of visible streaming with no
builds or JFR profiling during the run. Weighted mean painted rate was
**58.92 frames/s**, with minute windows **46.39–64.23 frames/s**. Their
observation-to-paint p95 values ranged from **26.18 to 42.10 ms**. Thus the route
gets close to the 60 fps target on this Mac, but did **not sustain 60 fps in every
window**. Other work on the shared Mac continued.

| Minute | Painted frames/s | Observation → paint p95, ms | JVM heap, MiB | Helper RSS, MiB |
|---:|---:|---:|---:|---:|
| 0 | — | — | 181.04 | 53.73 |
| 1 | 62.14 | 29.85 | 172.05 | 53.91 |
| 2 | 60.21 | 31.97 | 171.85 | 53.92 |
| 3 | 50.35 | 39.79 | 151.84 | 53.94 |
| 4 | 46.39 | 42.10 | 169.75 | 53.94 |
| 5 | 55.74 | 38.95 | 152.18 | 53.94 |
| 6 | 61.59 | 32.20 | 139.24 | 53.94 |
| 7 | 64.10 | 26.18 | 169.31 | 53.94 |
| 8 | 62.04 | 30.31 | 152.35 | 53.94 |
| 9 | 64.23 | 26.92 | 152.36 | 41.78 |
| 10 | 62.47 | 28.92 | 210.62 | 53.94 |

Mean CPU was **26.40% of one core for the helper**, **46.13% for the EDT** and
**89.64% for the whole IDE**. Helper RSS settled at 53.94 MiB after the first few
samples, dipped once, then returned; there was no continuing increase. IDE RSS
went from **2835.78 to 2025.47 MiB**, range **2025.47–2850.08 MiB**. Requested-GC
heap samples ranged **139.24–210.62 MiB**, ending above their 181.04 MiB start.
They are nonmonotonic and include the rest of Rider; this does **not establish
absence of IDE memory growth**. The test measures bounded samples, not leak freedom.

Its preceding short measurement painted **63.45 frames/s** for **13.175 s** at
1206×2622, with observation-to-paint p50/p95 **21.48/32.13 ms**. The EDT consumed
**6.615 CPU seconds** (**50.21% of one core**). Press-to-up/barrier acknowledgments
were **18.705, 5.187, 6.313, 6.156, 7.155, 5.747, 3.955, 6.162, 5.262, 4.874,
4.590 and 4.376 ms**. A real Swing drag painted **60 changing frames before release**.
Launch-request-to-next-paint was **247.01 ms**, with the same non-launch-completion
qualification as above.

After Home settled, there were **zero new paints in five seconds**. Hiding the
panel produced **zero paints and zero owned native helper processes after 1.5 s**.
The complete UI probe printed **`passed`**, and Gradle reported
**`BUILD SUCCESSFUL in 10m 42s`**, 18 tasks (5 executed, 13 up-to-date). The final
light/dark, narrow/wide and zoomed probe images were visually inspected. The owned
probe IDE, helper and Gradle processes exited; the owner's processes were untouched.

### Commands, tests and review

Commands below ran from `<worktree>/editors/jetbrains`, with
`JAVA_HOME=/opt/homebrew/opt/openjdk@25` and
`GRADLE_RO_DEP_CACHE=<cache-worktree>/editors/jetbrains/.gradle-home/caches`.
Placeholders remove personal paths and device identifiers. Socket/process checks
used the approved execution context outside the filesystem sandbox. No command
below fetched a dependency.

```sh
./gradlew --offline check buildPlugin verifyPlugin
./gradlew --offline runIde -PuiProbe
KUMIKAE_SIMULATOR_LIVE_PROBE=1 ./gradlew --offline runIde -PuiProbe
KUMIKAE_SIMULATOR_LIVE_PROBE=1 KUMIKAE_SIMULATOR_ENDURANCE_PROBE=1 \
  ./gradlew --offline runIde -PuiProbe
python3 -m unittest discover -s tests
cd native/SimulatorBridge
xcrun swift-format format --in-place --recursive Sources Tests
swift build -c release
swift test
```

The final full offline build printed **`BUILD SUCCESSFUL in 31s`** (37 tasks:
14 executed, 23 up-to-date). XML reports contain **105 panel tests and 62 core
tests**, zero failures, errors or skips. The verifier reports compatibility with
**RD-262.10968.170**, with four deprecated-API and six experimental-API notices
for existing IntelliJ integrations; it reports **0 B downloaded**. Python printed
**`Ran 7 tests in 3.552s`**, **`OK`**. Swift release printed
**`Build complete! (0.40 sec)`**; Swift Testing printed
**`Test run with 16 tests in 8 suites passed after 0.011 seconds`**. The earlier
XCTest compatibility runner's zero-test line is not the Swift Testing result.

Exploratory scripts and their outputs stayed in ignored `build/native-probes/`:

```sh
python3 -I build/native-probes/transport.py
python3 -I build/native-probes/gesture.py
python3 -I build/native-probes/endurance-dtu.py
xcrun dyld_info -disassemble /usr/lib/system/libxpc.dylib
jcmd <owned-probe-pid> JFR.start name=paint settings=profile duration=30s \
  filename=<temporary-recording>
jfr print --json --events jdk.ExecutionSample,jdk.NativeMethodSample <temporary-recording>
xcrun simctl launch <id> com.apple.Preferences
xcrun simctl io <id> screenshot <temporary-file>
```

Local Swift runtime-introspection probes inspected CoreSimulator/SimulatorKit and
IOSurface properties, tested Screen Recording preflight and timed the copies
reported above. The rejected callback probe is unsafe to rerun and is not shipped.
No captured screens, Apple headers, Apple binaries or exploratory disassemblies
are committed. The packaged executable/resource bundle was extracted to a temporary
folder and `simulator-bridge --version` printed
`SimulatorBridge 1; frame header 64 bytes`, confirming relocatable resources.

Failing-first assertions were observed for shared header validation, mapped-reader
newest sequence, native ownership, fallback ordering, touch coalescing, session
geometry, missing-symbol guard, stdin lifetime (including active input), DTUHID
message shape, first-frame readiness, startup/input exclusion, transient reconnect,
concurrent fallback status, cancelled startup, late resource disposal, nonblocking
feed close, cancellation during ready/ping waits, static device-body caching and
its allocation bound. Some early attempts failed compilation before the assertion
runs. **Swift mapped-sink tests, ASCII keyboard
mapping tests, and shared-repository touch delegation were not seen assertion-fail
first**; those are verification tests added with or after implementation. Live
measurement instrumentation was also added alongside its use.

Independent review inspected private ABI/ownership, cleanup, concurrency and error
paths. Reproduced findings were fixed and rechecked: an optional first frame before
ready; wrong endpoint assumptions; adapter reuse; permanent fallback after a
transient stop; a status-index race; a resource returned after worker disposal;
synchronous session callbacks during feed close; interruption masked by reply
readers; and the static-decoration cache allocation bound. The reviewer used no
live device actions.

### Compatibility and remaining limits

The complete private symbol/protocol list and absence handling are in the
[native package README](../../editors/jetbrains/native/SimulatorBridge/README.md#runtime-boundary).
One actor owns those names, with small extensions; frame source/sink, input sink
and device lookup are protocols. Runtime loads, symbol checks, Objective-C ABI
checks and a strict measured-Xcode-build guard fail with actionable errors. The
final helper loads CoreSimulator and libxpc, not SimulatorKit, and needs no idb
at runtime. Its startup fallback is native → installed idb → public screenshots;
unknown input is never replayed. `DEVICE_UNAVAILABLE` preserves the native route
for a later reconnect. The existing private-input switch controls both routes.

Actual Simulator shutdown/reboot recovery was **not run**, because the shared-device
rules explicitly forbid it. Fake process/device failures cover teardown, revoked
geometry and subsequent retry. Other Xcode builds, Intel Macs, iPad devices,
manual owner acceptance, optical latency and application input-response time
remain unverified. Native rotation is explicitly unsupported; text is printable
US ASCII and touch is single-finger. These, the strict Xcode guard, full-resolution
copies and 8 ms seed polling are decisions the owner may want changed.

Existing problems found while integrating were the low-resolution idb/33 ms UI
path, status calls starting input while hidden, stream opening competing with
input, worker cancellation dropping late resources, reply readers masking
interruption, and repeated rasterization of unchanged device decoration. Native
integration fixes those paths. Remaining synchronous legacy
session actions can still hold the session lock; native feed closure now signals
its worker and does not wait for that lock on the EDT.

The root Gradle change is one separate native-helper/probe block; the core build
adds the shared frame resources. No language/project/run source folder or
`plugin.xml` was changed.
