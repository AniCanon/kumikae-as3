# Native Simulator bridge plan

1. Read the plugin contracts and run the offline baseline. Probe installed private
   interfaces and ScreenCaptureKit availability without downloads.
2. Write failing Swift and Kotlin tests for a shared binary header and latest-frame
   transport. Compare raw pipe and mapped-slot cost; record the choice.
3. Add the Swift library and executable: guarded runtime adapter, frame pump, input,
   command validation and stdin lifetime. Keep private symbols in one adapter.
4. Add service-owned native process startup, fallback, geometry and local touch
   dispatch with fake-process tests. Keep HTTP version 1 compatible.
5. Wire continuous pointer input, move coalescing, visibility and 60 Hz painting.
   Add one separate native build block to Gradle; avoid language-plugin areas.
6. Run all required checks and inspect the UI probe pictures. Measure the actual
   native panel and ten-minute memory behavior where the shared-device rules allow.
7. Obtain independent review, address findings, update the results and architecture,
   then make small commits staged by file name. Report every remaining limitation.
