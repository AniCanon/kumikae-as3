# Simulator speed implementation plan

1. Measure the existing screenshots and per-event idb, dynamic and fixed MJPEG,
   and persistent idb shell input on the booted demo Simulator.
2. Add failing tests for bounded JPEG parsing, newest-frame delivery, owned process
   cleanup, persistent input, consent and scaled geometry. Implement core streaming
   and binary snapshots without changing the agent protocol.
3. Add manual-scheduler controller tests for hide, tab changes, device changes,
   disposal, retries and independent input. Implement subscription lifecycle and
   screen-only repainting. Update consent copy.
4. Run the complete offline build, plugin verification, UI probe and Python tests.
   Inspect UI images. Measure the real pipeline and a ten-minute stream.
5. Obtain an independent review, resolve its findings, update architecture, protocol
   and measured results, and commit by named files. Do not push or merge.

Review focus: no queued images or EDT decode, no capture lock blocking input,
geometry safety with simultaneous agent screenshots, cancellation races, bounded
memory and stderr, owned child cleanup, consent, and honest measurement limits.
