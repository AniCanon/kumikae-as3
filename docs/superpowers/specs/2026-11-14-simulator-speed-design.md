# Live Simulator picture and input

The owner requested this design and implementation together. Keep Kotlin, Compose Swing,
the existing Gradle dependencies and the version 1 agent protocol.

Use the in-process session for panel pictures. A subscription carries binary JPEG bytes
from idb directly to a bounded newest-frame slot, then decodes off the EDT. The existing
HTTP JSON interface remains available to agents, including full-size screenshots. This
avoids another server protocol and base64 allocation in the panel's hot path.

With the owner's existing idb switch on, start a private companion on a session-owned
socket and a persistent `idb shell --no-prompt` for input. The installed shell already
acknowledges each command and keeps its gRPC connection open. No Python helper, gRPC
library, codec or dependency is added. A separate `idb video-stream` child uses dynamic
MJPEG at one-third scale and quality 0.6. Dynamic mode follows screen changes instead
of forcing duplicate frames on a still screen. The source measured about 58 fps during
Settings swipes; a fresh input process measured 341–361 ms per tap.

The controller owns subscription state, visibility, generation, failure status and retry
backoff. Only the newest encoded and decoded frames are retained. A worker decodes
pictures, and a coalesced EDT delivery updates the native screen. Normal frame changes
repaint only its screen rectangle. Polling status, logs and devices stays independent.
The list tab and a hidden window stop streaming. Device changes and disposal close it.
Failures restart after 0.5, 1, 2, 4 and at most 8 seconds, with visible status.

The service records native geometry separately from the streamed image dimensions.
Input quotes the generation and displayed dimensions; conversion uses the actual ratio
to native pixels and idb's density. Screenshots and streaming can coexist without
invalidating one another. Capture and decode run outside the session lock. The lock
protects selection and geometry, so input cannot wait behind a stream read or decode.

Without consent, retain simctl screenshots. Use the in-process binary path for the panel
and skip pictures when its device tab is hidden. HTTP screenshots remain unchanged.
Every child is explicitly owned and closed; never terminate an existing companion.

Validation: failing-first parser, newest-frame, process, geometry and controller tests;
offline full checks and UI probe; real Simulator throughput, delay, CPU, tap latency,
ten-minute memory and process cleanup measurements. Report any target not demonstrated.
