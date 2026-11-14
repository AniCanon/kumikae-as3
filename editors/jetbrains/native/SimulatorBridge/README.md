# SimulatorBridge

A service-owned Swift 6 helper for the Simulator panel. Build with `swift build -c
release`; test with `swift test`. There are no package dependencies. Public Apple
frameworks supply memory mapping and images; private interfaces load only at runtime
after the panel's private-input switch. See `../../protocol.md` for its transport.

`SimulatorBridge` is the library; `SimulatorBridgeCLI` is only the composition root.
Keep private symbol names in the runtime adapter. Device lookup, frame source, frame
sink and input sink are protocols. Mutable native objects stay actor-isolated.
No force unwraps, unchecked sendability or silent error recovery in production code.
Aim for fewer than 300 lines per file. Tests use Swift Testing and fake boundaries.

The shared frame layout in `Sources/SimulatorBridge/Resources` belongs to both
Swift and Kotlin. Change its version when compatibility changes. No Apple headers,
binaries, device identifiers or captured screen contents belong in this folder.

## Runtime boundary

`AppleSimulator` is the private adapter, split into files for display lookup,
connection ownership, and input messages. `RuntimeSymbols` only implements public
`dlopen`/`dlsym` and Objective-C runtime operations. Private frameworks are not
linked at build time. The adapter deliberately accepts only Apple silicon and
Xcode 27.0 build 27A266a, measured on macOS 27.2.1 with iOS 26.4. A changed build
requires inspection and measurements before widening that guard.

The complete private surface used by the helper is:

| Loaded image or object | Class, selector, export or protocol name |
|---|---|
| Runtime-loaded CoreSimulator framework | `SimServiceContext`; `sharedServiceContextForDeveloperDir:error:` |
| Service context and device set | `defaultDeviceSetWithError:`, `availableDevices` |
| Device | `UDID`, `state`, `bootGeneration`, `io`, `deviceType`, `lookup:error:` |
| Device I/O and port | `ioPorts`, `descriptor` |
| Display descriptor and properties | `screenProperties`, `uniqueId` (`PurpleMain`), `framebufferSurface` |
| Device type | `productFamilyID`, `mainScreenScale` |
| Runtime-loaded system libxpc | `xpc_endpoint_create_mach_port_4sim`, `xpc_connection_enable_sim2host_4sim` |
| Guest DTUHID service | `com.apple.coredevice.feature.remote.hid.digitizer` |
| Private XPC message types | `IndigoDigitizerEvent`, `IndigoButtonEvent`, `IndigoKeyboardButtonEvent` |

Every callable class, selector and export is checked before use; Objective-C
argument and result encodings must match. Guard failures name the selected Xcode
version/build and missing or changed symbol. C export signatures have no runtime
introspection: the strict build guard covers that limitation. Missing display,
service or endpoint also fails startup. The service falls back to installed idb,
then screenshots. A later device failure ends the feed and revokes its geometry;
reopening starts a new process. A typed `DEVICE_UNAVAILABLE` startup error keeps
the native backend available for a later boot/reconnect; it does not exhaust the
fallback list. Unknown input delivery is never replayed.

The XPC envelope uses `messageType`, `featureIdentifier`, `isBarrier` and `payload`.
Touch payloads use normalized `pointOne.x/y`, `eventType` (down/move/up), `edge`
and `target`. Button payloads use `usageCode`, `state` and, for consumer buttons,
`usagePage`. These private values live only in the adapter. Each actual input
message is followed by a separate barrier; its reply means ordered receive-queue
progress in the guest input service, not application handling. The barrier itself
must carry no input payload. No SimulatorKit, legacy HID client, callback
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
polls seeds every 8 ms; it has no private screen callback ABI. The mapped slot drops
a frame under lock contention. Only iPhone, printable US ASCII, single touch, Home
and Lock are implemented; native rotation fails explicitly. Lock and device reboot
recovery remain live-device checks for a later authorized session.
