# Native Simulator bridge

The owner requested a Swift 6 package, direct pictures and continuous touch input,
behind the existing private-interface switch. This design implements that scope.

## Route and evidence

Use CoreSimulator's main display IOSurface, loaded at runtime. On the installed
Xcode 27.0, the read-only probe obtained 1206 by 2622 BGRA pixels with a 4864-byte
row. Most full-surface copies took 0.2–0.3 ms. Screen Recording preflight returned
false, so ScreenCaptureKit cannot capture here without additional permission.
That is an availability comparison, not a measured performance comparison.

Do not register private screen callbacks. An exploratory call with absent optional
callbacks coincided with two SimRenderServer crashes and dependent backboardd
failures. The server fault dereferenced a null callback. The helper will instead
sample the IOSurface seed and copy only changes. No callback ABI is required.
Its timestamp means first observation of a changed surface, not presentation.

Private symbols belong to one actor, split into small extensions for connection
and input messages, behind device lookup, frame source and input sink protocols. A runtime loader guards class, selector and C-symbol availability
and reports the selected Xcode version. No Apple headers or binaries are copied. Own runtime ABI declarations are guarded
against the installed method signatures and the measured Xcode build. The helper is a child of the service and exits on stdin EOF.

Input uses the guest DTUHID XPC service. The older mach/Indigo route acknowledged
writes but produced no visible movement on this runtime. Read-only inspection of
the installed receiver established DTUHID's normalized coordinates, phase values,
button states and barrier semantics. Real Settings scrolling, Home and text then
confirmed the new route. SimulatorKit is not loaded by the final helper.

## Transport and state

Use a single memory-mapped frame slot. For a 12.75 MB payload, the local Python
transport harness measured median 0.47 ms versus 1.63 ms for a pipe (100 samples).
This excludes source capture, JVM copy and painting. Pixels never use JSON or base64. A shared resource defines header
offsets, limits and format; Swift and Kotlin both load it. OS record locks protect
the mapped slot, so a reader never observes a partially written picture. A busy
slot drops the frame. There is no frame queue. Copying belongs to a service worker.

The service owns one helper per selected device. Startup initializes input before
reporting ready. Native startup failure tries installed idb, then screenshots and
reports the selected route. A failed input is never replayed through a fallback.
Existing HTTP requests remain compatible. A local touch seam carries press, newest
move and release; moves coalesce, while press and release retain their order.

The panel samples frames frequently enough for a 60 Hz source and repaints only
the screen. Hiding or switching tabs ends the frame subscription and its helper.
Device changes and geometry changes invalidate input coordinates. Late work after
closing is discarded. Reopening starts a fresh connection after device failure.

## Validation and limits

Use failing-first tests for the shared header, newest-frame delivery, touch
coalescing, helper ownership, fallback and missing symbols. Run the existing offline
Kotlin, Python and UI checks, plus release Swift build and Swift tests. An independent
review checks lifetimes, private API guards and failure paths.

Measure source observation to paint separately from actual Simulator presentation
latency. Measure input acknowledgment separately from application response. Record
resolution, paint rate, CPU and ten-minute memory samples. Simulator shutdown and
boot are forbidden on this shared device, so that live check stays unverified;
fake lifecycle tests cover reconnection instead. No performance target is claimed
without its measurement.
