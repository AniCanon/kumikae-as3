# AS3 source test actions — 2026-11-16

The sample has 52 named scenarios across six fixture files, preserving its 93
model assertions. Manifest mappings connect each case identity to a source method.
The class gutter/file context selects that file; the method action selects its
registered cases. Helpers and nested function declarations have no Run icon.

Error failures preserve AS3 stack traces. The native Tests tree opens the first
application frame at its assertion line; individual stack frames are clickable.
Paths resolve against the manifest directory, including nested manifests. Thrown
values without a stack fall back to the registered method instead of a guessed line.

## Observed checks

The plugin check after incorporating the latest main completed with 193 plugin
tests (one opt-in skip) and 65 core tests (three opt-in skips), no failures.
Both installed IDE builds were accepted by Plugin Verifier: RD-262.10968.170 and
IU-261.22158.277 with LSP4IJ 0.21.0.

- Framework Python suite: 19 tests passed, including real compiler lifecycle,
  assertion/setup/cleanup stack locations and parameter-case mappings.
- Plugin Python client/template suite: 9 tests passed, including the native Godot template.
- Compiler CTest: `100% tests passed out of 26`.
- Refreshed desktop extension: `godot: OK` from the Godot parity runner.
- Sample selection: `PASS 52 individual cases, six file selections, full model suite and missing-case rejection`.
- Sample game: `PASS SUITE 76 integration checks plus 93 model assertions`,
  responsive layout and audio checks; `PASS all Support Sprint checks`.

Regressions were observed failing before correction: entry-file case mappings,
object-valued method defaults, accessor helper contexts, nested function gutters,
shared-method parameter registration, and discarded assertion stack locations.
The real native result probe also caught a source-location problem before its
fixture and navigation checks passed.

The native runtime was rebuilt from current engine sources using existing
Godot C++ bindings/archives. Desktop integration was rerun. Device and simulator
libraries built successfully; no new mobile export or physical-device run was
performed for this change. No dependencies were downloaded.

The standalone sample is maintained outside this engine repository. Its generated
suite registration and local framework copy are synchronized; the normal sample
runner now rejects stale generated case registration.
