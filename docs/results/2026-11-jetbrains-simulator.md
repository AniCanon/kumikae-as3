# JetBrains Simulator checks

Date: 2026-11-14. Work on `feat/jetbrains-simulator`.

The [design](../superpowers/specs/2026-11-14-jetbrains-ios-simulator-design.md)
was committed before the implementation. Code and build instructions are in
[`editors/jetbrains/`](../../editors/jetbrains/).

## Where it stands now

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
