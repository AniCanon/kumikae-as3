# Rider project experience — 2026-11-16

Implemented in the JetBrains plugin and `testing/`, with the Support Sprint sample
using the shared AS3 assertions and a registered test manifest.

## Delivered behavior

- Opening `project.godot` provides General, Display and Export settings beside source.
  Name, scene, icon preview, viewport, orientation, stretch and per-preset identifiers
  are editable. Unknown configuration content is preserved. Conflicting document
  changes are refused; writes are undoable. Preset switching retains pending edits.
- Game view compacts empty AS3 package chains under `src` and `tests`. Generated
  folders are hidden only at the project root. Both options are persistent toggles.
- Scene files show nodes and attached scripts, with attach/detach/open actions and
  reverse navigation from AS3. Attachment edits require a directly compatible native
  base; inherited and instanced scenes remain editable in Godot.
- Kumikae Testing supplies AS3 assertions, named fixtures, setup/cleanup and typed
  constructor-parameter cases. Rider displays native test results and reruns failures.
  Explicit suite registration respects the current interpreter; metadata discovery,
  function callbacks and parallel cases are not implemented.
- The sample plays a confirmation when sound is enabled and immediately stops
  playback when muted. Gameplay audio remains driven by state changes.

## Observed checks

On macOS, using the installed Rider 2026.2.3.1/JBR and Godot 4.7.2:

| Check | Observed result |
|---|---|
| `./gradlew --offline check buildPlugin -Pkas3Root=../../../..` | `BUILD SUCCESSFUL`; 133 JVM tests, zero failures |
| Plugin Python discovery | 8 passed, 1 template test skipped because the isolated worktree has no native build |
| Template test rerun against the existing checkout library | 1 passed; real Godot swipe, tap and stray-touch checks |
| `KAS3C=<existing compiler> python3 -m unittest discover -s testing/tests -v` | 14 passed, including actual AS3 lifecycle execution |
| `./gradlew --offline runIde -PuiProbe -Pkas3Root=../../../..` | `BUILD SUCCESSFUL`, `result.txt: passed`; light/dark settings, validation and scene actions exercised and PNGs inspected |
| Native test-results UI probe | One passing and one failing leaf displayed correctly |
| Completion UI probe | Three disposable projects; lookup, Tab/Enter, undo, changed-document cancellation and argument snippets passed |
| Sample `python3 tests/run.py` | `PASS all Support Sprint checks`: 93 model assertions, 76 integration checks, compiler, scene smoke, layout and audio |
| Sample through bundled-runner source | `AS3 tests: 4 passed, 0 failed`; six model cases reported individually |
| Filtered model run | Only the requested Input ownership case ran; mixed valid/missing requests fail |
| Exported iOS test app on a disposable Simulator | `PASS SUITE 76 integration checks plus 93 model assertions`, including native playback start/stop |
| Repeated real-time audio check | Five consecutive runs passed, observing playback or its native completion signal |
| Orientation export | Landscape and portrait settings reached the generated app orientation entries |

The sandbox emitted a Rider Problems View `HighlightingPanel` disposal diagnostic
at exit despite passing probes and a zero exit status. No claim of leak-free IDE
shutdown is made. The disposable simulator was removed after verification.

The independent reviewer found and verified fixes for timeout cleanup, preset
edits, invalid orientation, completion markers, native case events, scene base
validation, case selection and duplicate entry-source expansion. Regression tests
were observed failing before those fixes. No review findings were deferred.

## Limits

The plugin ZIP is built; the owner's installed Rider plugin was not replaced.
UI checks drive real controls with synthetic repositories; model tests cover
configuration and scene transformations. Export checks use the sample and installed
native tools. Physical audible output, Android device exports and other operating
systems were not observed. Playback assertions establish native player state.
No compiler source changed; the full compiler suite was not rerun.
