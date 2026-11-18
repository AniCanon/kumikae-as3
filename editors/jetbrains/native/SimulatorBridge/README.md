# SimulatorBridge

A Swift helper for the plugin's iOS Simulator panel. The Simulator service starts it to read a simulator's
screen at full resolution and to send touches, keys and rotation. It runs only after the user switches
input on in the panel, because it uses private Apple interfaces.

This page is for someone changing the helper. [protocol.md](../../protocol.md#the-native-helpers-transport-version-1)
specifies how the service and the helper talk.

## Build and test

```sh
swift build -c release
swift test
```

The package needs Swift 6 and has no dependencies. From the plugin's folder, `./gradlew
buildSimulatorBridge` builds it and bundles the executable with the plugin.

Lint and format with the swift-format that ships with Xcode:

```sh
xcrun swift-format lint --strict --recursive Package.swift Sources Tests ProbeApp
xcrun swift-format format --in-place --recursive Package.swift Sources Tests ProbeApp
```

From the plugin's folder, `lintSimulatorBridge` and `formatSimulatorBridge` run the same commands. The
plugin's `check`, `buildPlugin` and `buildSimulatorBridge` tasks all require the lint to pass.

## Structure

| Part | Job |
|---|---|
| `SimulatorBridge` | The library |
| `SimulatorBridgeCLI` | The executable, `simulator-bridge`. It only wires the library together |
| `AppleSimulator*.swift` | The one adapter that knows private Apple names: display lookup, the connection, input messages, orientation |
| `RuntimeSymbols.swift`, `PrivateAPIGuard.swift` | Loading a framework at run time, and checking a class, selector or export before it's used |
| `BridgeRunner.swift`, `CommandChannel.swift` | Reading commands, publishing frames and handling input |
| `MappedFrameSink.swift`, `FrameLayout.swift`, `FrameHeader.swift` | The frame file the service reads |
| `Resources/frame-layout.json` | The frame layout, shared with the Kotlin side |
| `ProbeApp/` | A test app for the probes below |

## Rules

- **Private names stay in the adapter.** Device lookup, the frame source, the frame sink and the input
  sink are protocols. Everything else is written against them, and the tests use fakes.
- **Private frameworks are loaded at run time,** never linked at build time. No Apple header or binary is
  in this folder.
- **Every private call is checked before it's used.** A class, selector or export that's missing, or
  whose Objective-C signature has changed, fails startup with an error that names the Xcode version and
  the symbol. For a C export, only its presence can be checked.
- **Mutable native objects stay inside an actor.** The package builds in Swift 6 language mode, with
  sendability checking on.
- **No force unwraps, no unchecked sendability, no silent error recovery.** The `.swift-format` file
  rejects force unwraps, `try!` and implicitly unwrapped optionals.
- **An adapter is used once.** Reconnecting starts a new helper process, so that a callback from an old
  connection can't affect a new one.
- **Input whose delivery is unknown is never sent again.**
- **Change the layout's version when compatibility changes.** `frame-layout.json` belongs to both the
  Swift and the Kotlin side.
- **No device identifier or captured screen content in this folder.**

## The private interfaces

The helper accepts simulators on a Mac with Apple silicon. Whether an installed Xcode is supported is
decided by checking for the interfaces below at run time. A small deny list can refuse a build that's
known not to work. It's empty. Only Xcode 27.0 has been tested.

This is everything private the helper uses:

| Loaded image or object | Class, selector, export or protocol name |
|---|---|
| The CoreSimulator framework | `SimServiceContext`; `sharedServiceContextForDeveloperDir:error:` |
| Service context and device set | `defaultDeviceSetWithError:`, `availableDevices` |
| Device | `UDID`, `state`, `bootGeneration`, `io`, `deviceType`, `lookup:error:` |
| Device I/O and port | `ioPorts`, `descriptor` |
| Display descriptor and properties | `screenProperties`, `screenType`, `uiOrientation`, `framebufferSurface` |
| Device type | `productFamilyID`, `mainScreenScale` |
| The system's libxpc | `xpc_endpoint_create_mach_port_4sim`, `xpc_connection_enable_sim2host_4sim` |
| The device's input service | `com.apple.coredevice.feature.remote.hid.digitizer` |
| The device's orientation service | `com.apple.coredevice.feature.remote.devicecontrol.orientation` |
| Private XPC message types | `IndigoDigitizerEvent`, `IndigoButtonEvent`, `IndigoKeyboardButtonEvent` |

The helper ships no SimulatorKit, no private header and no Apple binary. It opens no Simulator window and
needs no Screen Recording permission.

## How it works

### The screen

- **The display** is the device's one built-in screen (`screenType` 0). External screens are ignored.
- **Capture polls** the screen's surface on an 8 ms budget and copies only when the surface has changed.
  There's no private screen callback.
- **Frames go into a memory-mapped file.** If the reader holds the lock, the helper drops that frame.
- **Rotation.** The helper rotates the pixels with Accelerate and applies the inverse transform to touch.
  A change of orientation ends the feed, and a new helper reads the new orientation.

### Input

- **Each input message is followed by a separate barrier message.** The barrier's reply shows that the
  device's input service received the input in order. It doesn't show that the app handled it. The
  barrier carries no input itself.
- **Supported:** a single touch (down, move, up), Home, Lock, rotation, printable ASCII as key presses,
  and other text through the device's pasteboard and Command-V.
- **Not supported:** multi-touch and audio.
- **Rotation uses a separate connection** to the orientation service, and waits for its reply.

### Shutdown and failure

Closing the helper's standard input cancels work in progress and releases the input connection and the
device. A device that fails later ends the feed, and the service starts a new helper. If the device is
stopped or restarting, startup fails with `DEVICE_UNAVAILABLE`, which the service retries.

## Limits

- **No presentation timestamp.** The helper knows when it observed a changed surface, not when the
  Simulator presented the frame. Timings that start at the observation don't include what came before it.
- **An app can restrict its orientations, refuse a paste, or restrict secure fields.**
- **Lock** is implemented and hasn't been confirmed by eye on a device.

## Probes

Three opt-in tests drive a real Simulator. Each creates one disposable device, uses only that device, and
deletes it at the end, also after a failure. None starts an IDE. Build the release helper first, and run
them from the plugin's folder.

| Probe | Command | Checks |
|---|---|---|
| Lifecycle | `KUMIKAE_NATIVE_LIFECYCLE_PROBE=1 ./gradlew --offline :core:test --tests '*NativeLifecycleProbe'` | Shutdown, reboot and reselection |
| Interaction | `KUMIKAE_NATIVE_INTERACTION_PROBE=1 ./gradlew --offline --no-daemon :core:test --tests '*NativeInteractionProbe'` | Rotation, taps on targets, and exact Unicode text. `KUMIKAE_NATIVE_PROBE_FAMILY=iPad` uses a tablet |
| Performance | `KUMIKAE_NATIVE_PERFORMANCE_PROBE=1 KUMIKAE_NATIVE_PROBE_LABEL=run-1 ./gradlew --offline --no-daemon :core:test --tests '*NativePerformanceProbe'` | Ten minutes of frames at 60 Hz with touches at 50 Hz. Reports go to `core/build/reports/native-performance/` |

The performance probe measures delivery from the helper to the reader. It doesn't measure the panel's
painting or an app's response to a touch. Use a new label for each run, and don't run a build during the
measurement. `KUMIKAE_NATIVE_PROBE_SECONDS` shortens the run to between 30 and 600 seconds.

Set `KUMIKAE_NATIVE_METRICS` to the path of a new file to make the helper write its own timings there, in
ten-second windows.
