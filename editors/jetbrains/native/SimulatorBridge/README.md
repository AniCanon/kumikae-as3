# SimulatorBridge

A service-owned Swift 6 helper for the Simulator panel. Build with `swift build -c
release`; test with `swift test`. There are no package dependencies. Public Apple
frameworks supply memory mapping and images; private interfaces load only at runtime
after the panel's private-input switch. See `../../protocol.md` for its transport.

`SimulatorBridge` is the library; `SimulatorBridgeCLI` is only the composition root.
Keep private symbol names in the runtime adapter. Device lookup, frame source, frame
sink and input sink are protocols. Mutable native objects stay actor-isolated.
No force unwraps, unchecked sendability or silent error recovery in production code.
Prefer readable spacing and cohesive files; 300 lines is a guideline, not a reason to
compress code. Tests use Swift Testing and fake boundaries.

The shared frame layout in `Sources/SimulatorBridge/Resources` belongs to both
Swift and Kotlin. Change its version when compatibility changes. No Apple headers,
binaries, device identifiers or captured screen contents belong in this folder.

## Swift style and ownership

The package keeps Swift tools 6.0 and Swift 6 language mode; this cleanup was built
with installed Swift 6.4. Actor isolation and Sendable checking remain enabled.
`FrameLayout.read` borrows its input bytes synchronously. `FrameRotation.apply`
consumes a frame that it either forwards unchanged or replaces with rotated pixels.
Other parameters use Swift's normal ownership rules. Retained frames remain copyable
and keep Data's copy-on-write protection. Ownership annotations alone make no
performance guarantee.

Input wire names are converted to `InputOperation` before exhaustive dispatch.
Replies use Encodable values; a failed reply write ends command processing without
retrying the command reply. The runner separates command intake, frame publication and input
handling, and retains both errors when work and cleanup fail.

Use Xcode's installed swift-format; no package or downloaded linter is required:

```sh
xcrun swift-format lint --strict --recursive Package.swift Sources Tests ProbeApp
xcrun swift-format format --in-place --recursive Package.swift Sources Tests ProbeApp
```

The checked-in `.swift-format` sets two-space indentation and 100-column formatting,
keeps intentional line breaks, and rejects force unwraps, force-try and implicitly
unwrapped optionals. From the plugin directory, `lintSimulatorBridge` and
`formatSimulatorBridge` run these commands. `check`, `buildPlugin` and
`buildSimulatorBridge` require lint. Native tasks skip when installed Swift on macOS
is unavailable; if Swift exists but swift-format is missing, lint fails with the
Xcode tool lookup error. Generated `.build` files are outside the lint paths.

## Runtime boundary

`AppleSimulator` is the private adapter, split into files for display lookup,
connection ownership, and input messages. `RuntimeSymbols` only implements public
`dlopen`/`dlsym` and Objective-C runtime operations. Private frameworks are not
linked at build time. The adapter accepts Apple silicon and gates Xcode support on
its runtime capabilities. A small build deny list can refuse a demonstrated
incompatibility with its reason; it is currently empty. Xcode 27.0 build 27A266a
is the only build measured so far, on macOS 27.2.1 with iOS 26.4.

The complete private surface used by the helper is:

| Loaded image or object | Class, selector, export or protocol name |
|---|---|
| Runtime-loaded CoreSimulator framework | `SimServiceContext`; `sharedServiceContextForDeveloperDir:error:` |
| Service context and device set | `defaultDeviceSetWithError:`, `availableDevices` |
| Device | `UDID`, `state`, `bootGeneration`, `io`, `deviceType`, `lookup:error:` |
| Device I/O and port | `ioPorts`, `descriptor` |
| Display descriptor and properties | `screenProperties`, `uniqueId` (`PurpleMain`), `uiOrientation`, `framebufferSurface` |
| Device type | `productFamilyID`, `mainScreenScale` |
| Runtime-loaded system libxpc | `xpc_endpoint_create_mach_port_4sim`, `xpc_connection_enable_sim2host_4sim` |
| Guest DTUHID service | `com.apple.coredevice.feature.remote.hid.digitizer` |
| Guest orientation service | `com.apple.coredevice.feature.remote.devicecontrol.orientation` |
| Private XPC message types | `IndigoDigitizerEvent`, `IndigoButtonEvent`, `IndigoKeyboardButtonEvent` |

Every callable class, selector and export is checked before use; Objective-C
argument and result encodings must match. Guard failures name the selected Xcode
version/build and missing or changed symbol. C export signatures have no runtime
introspection: only their presence is checked. Acceptance of another build is
not evidence of a live test on that build. Missing display,
service or endpoint also fails startup. The service falls back to installed idb,
then screenshots. A later device failure ends the feed and revokes its geometry;
reopening starts a new process. A typed `DEVICE_UNAVAILABLE` startup error keeps
the native backend available for a later boot/reconnect; it does not exhaust the
fallback list. An empty input-service startup barrier that times out after reboot
also reports `DEVICE_UNAVAILABLE`. This classification applies before any real
input is submitted; capability failures still permit fallback. Unknown input
delivery is never replayed.

The XPC envelope uses `messageType`, `featureIdentifier`, `isBarrier` and `payload`.
Touch payloads use normalized `pointOne.x/y`, `eventType` (down/move/up), `edge`
and `target`. Button payloads use `usageCode`, `state` and, for consumer buttons,
`usagePage`. These private values live only in the adapter. Each actual input
message is followed by a separate barrier; its reply means ordered receive-queue
progress in the guest input service, not application handling. The barrier itself
must carry no input payload. Orientation uses a separate connection and a `payload.changeOrientation._0`
request. Its dictionary reply must confirm `currentDeviceOrientation`. The helper
suspends frames once rotation is requested; the service closes that feed only
after the reply or failure. A new helper reads the oriented display metadata.
Public Accelerate rotates BGRA pixels; input uses the inverse transform. External
orientation changes end the feed even if dimensions stay the same.
No SimulatorKit, legacy HID client, callback
registration, private header or Apple binary is shipped.

The private endpoint factory consumes the device lookup's Mach send right. The
public XPC connection owns its own send right; releasing the endpoint object
releases the original lookup right. The adapter cancels that
connection and releases device/display references at disconnect; framework images
stay loaded until process exit because they register asynchronous runtime work.
The actor is single-use so callbacks from an old connection cannot affect a new one.

## Measurements and limits

The [results](../../../../docs/results/2026-11-jetbrains-simulator.md) distinguish
seed-observation-to-paint timing from actual presentation latency. No authoritative
presentation timestamp is available. The current route reads full-size BGRA and
targets an 8 ms seed-poll interval including copy/publication work; it has no private
screen callback ABI. Actor-owned pixel storage is reused with Data copy-on-write
protection for retained frames. The mapped slot drops
a frame under lock contention. iPhone and iPad, printable US ASCII key events, Unicode through the guest pasteboard and
Command-V, single touch, Home, Lock and rotation are implemented. Unicode may need
the guest app’s Allow Paste confirmation; paste restrictions and secure fields
remain under the app’s control. Control characters are refused. The guest app may restrict its orientations. Shutdown, reboot and
selection recovery were checked on a disposable device, including a startup
readiness failure fixed in this pass. Lock remains a separate live-device check.


Optional `KUMIKAE_NATIVE_METRICS` names a new, private output file. Ordinary runs
leave it unset. Ten-second JSON windows contain aggregate copy, seed, publication,
actor and barrier timings plus at most 64 slow input traces; overflow is counted.
The trace's `inputReturnedNanos` precedes the wire reply's `completedNanos`.
A trace keeps the last barrier of a command, so phase decomposition is meaningful
for single touch commands, not multi-key text. Barrier callback time includes host
callback scheduling and must not be labelled guest processing time.

After building the release helper, run this from the plugin folder for owned-device
rotation, target taps and exact Unicode text:

```sh
KUMIKAE_NATIVE_INTERACTION_PROBE=1 ./gradlew --offline --no-daemon :core:test \
  --tests '*NativeInteractionProbe'
```

The default family is iPhone; add `KUMIKAE_NATIVE_PROBE_FAMILY=iPad` for a tablet.
The probe may accept the guest paste permission only on its disposable fixture.
Its dialog locator is specific to the measured runtime; exact guest text is the
success criterion, including when no permission prompt appears.

```sh
KUMIKAE_NATIVE_PERFORMANCE_PROBE=1 KUMIKAE_NATIVE_PROBE_LABEL=run-1 \
  ./gradlew --offline --no-daemon :core:test --tests '*NativePerformanceProbe'
```

Choose a fresh label for each run. This runs 600 seconds of a 60 Hz guest fixture
and 50 Hz touch requests. The optional
`KUMIKAE_NATIVE_PROBE_SECONDS` accepts 30–600 for diagnosis. It records helper
metrics, frame/tick timelines, input intervals, natural JVM GC counts and bounded
clock calibration under `core/build/reports/native-performance/`. It verifies an
on-screen tick and its inverse, plus guest display-link/draw reports. No forced
GC, process sampling, IDE, host UI or other-device input occurs in this probe.
Do not overlap builds with the measured interval. This measures helper-to-reader
delivery, not panel paint or application response to each touch. Both probes build
the fixture with the installed SDK and delete their owned device in `finally`.
