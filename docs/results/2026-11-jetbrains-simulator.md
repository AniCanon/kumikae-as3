# JetBrains Simulator checks

Date: 2026-11-14. Work on `feat/jetbrains-simulator`.

The [design](../superpowers/specs/2026-11-14-jetbrains-ios-simulator-design.md)
was committed before the implementation. Code and build instructions are in
[`editors/jetbrains/`](../../editors/jetbrains/).

## What was run

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

## What has not been verified

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

First supply the missing annotation artifact and its metadata through an approved
dependency download. Run the Kotlin tests and plugin build. Fix any compiler or
test failures before calling the first version usable. Start the sandbox Rider
and inspect the panel against the home screen and Settings, including resizing,
hidden-panel polling, agent/IDE device changes, errors and project close.

Then decide whether to adopt idb's private interfaces. A compatible client and
companion must be selected, installed and tried together. Check tap targets at
each edge, a swipe, text, home, lock and both landscape rotations. Check that the
agent and panel operate the same device. Until then, use Device Hub for input.

When the Godot Simulator export is ready, install it explicitly, launch
`org.kumikae.as3test`, inspect its screen and logs, and confirm the report path.
This work does not complete the roadmap's AS3 debugger gate.
