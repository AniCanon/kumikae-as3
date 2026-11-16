# Completion follow-up: November 2026

Compiler measurements were made on 2026-11-15 on macOS arm64 with Apple Clang 21 and C++17 Release
builds. Plugin validation on 2026-11-16 uses stock Rider RD-262.10968.170 and free unified IntelliJ IDEA
IU-261.22158.277 with JDK 25; IDEA uses LSP4IJ 0.21.0. Android Studio is outside the scope.

## Behavior covered

- Typed member chains, lexical scope, imports, Enter insertion and Tab replacement, argument snippets,
  parameter help, go to declaration and outline.
- Automatic imports and generated method/accessor overrides, including empty names, metadata,
  CRLF and built-in methods. Acceptance changes all edits atomically; Undo restores the original text.
- Seven AS3 statement templates, placeholder navigation and linked catch names.
- Local pairing and indentation, continued typing/Backspace in an open lookup, and stale lookup cancellation.
- Diagnostic clearing, an 80-line document shrink and forty replacements, then restored language features.
- Split Problems ownership, close/reopen with the visible layout restored, concurrent projects and frame reuse.

Rider's diagnostic adapter publishes its native cache under read access. IDEA rejects stale ranges before
annotation construction. The Rider lifetime repairs ship in the plugin and run only on its tested build.
They preserve the split view and native startup behavior, with project-owned cleanup. The plugin requires
restart; it does not patch the IDE installation.

## Validation

The latest validation is **blocked**: `verifyPlugin` rejects the internal
`LSPClientFeatures.getServerWrapper()` call in `As3Lsp4ijClient`. The final compatibility matrix
therefore did not run. Earlier native IDEA publication checks passed three project cycles; the
stock Rider endurance result below predates the final IDEA-only changes. Claude's final review also
identified a race in the diagnostic probe's cleanup, which can clear a real server publication.
Both findings remain unfixed in this handoff.
The previous compiler validation remains applicable: Release **26/26**, ASan/UBSan **26/26**, and
`godot: OK`. Those runs took 27.87 s and 289.61 s respectively. No compiler code changed in this
plugin-lifetime follow-up. Python client tests: **9 passed**.

From `editors/jetbrains`, using the installed JDK 25:

```sh
./gradlew --offline check buildPlugin verifyPlugin
python3 -m unittest discover -s tests -v
./gradlew --offline runIdeaWithoutLsp4ij -PuiProbe
./gradlew --offline runIdea -PuiProbe
KUMIKAE_COMPLETION_RETENTION_CHECK=1 ./gradlew --offline runIde -PuiProbe
KUMIKAE_COMPLETION_RETENTION_CHECK=1 ./gradlew --offline runRiderWithoutLsp4ij -PuiProbe
KUMIKAE_COMPLETION_RETENTION_PROBE=1 ./gradlew --offline runIde -PuiProbe
```

The retention probe keeps only weak references to closed projects and requires zero survivors. Its JVM
collects soft caches at GC and allows ten seconds for native delayed work. This distinguishes collectible
UI snapshots from strong leaks. It does not clear platform caches or alter production JVM settings.
The stock Rider run completed ten project cycles in **600.32 seconds**, with **0/10** closed projects
retained at the final check. Separate reopen and concurrent/frame-reuse checks ended at **0/2** and
**0/3**. Whole-IDE heap after collection ranged from **194.97 to 220.63 MiB**; those samples do not prove
zero overall heap growth or that every platform lifetime is leak-free.

## Compiler performance

These are the earlier compiler measurements, not new timings for the Kotlin lifecycle repairs.
Baseline `c38c55a` and follow-up `dfc33c9` use the same Godot declarations. Runs alternate binary order,
include stdio/JSON decoding, and use a 5,003-line fixture plus a variant with 100,000 generated lines.
Recovery edits contain an incomplete member expression; valid-body edits change a comment after a clean
check. Builds and sandbox IDEs were stopped for these measurements.

```sh
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-completion-baseline --runs 101
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-completion-baseline --runs 101 --seconds 600 --valid-body
```

### Final server latency

Values are median / p95 / maximum in milliseconds, 101 samples per workload and build.

| Fixture and request | Merged baseline | Follow-up |
|---|---:|---:|
| Recovery, Small, warm | 0.125 / 0.139 / 0.309 | 0.138 / 0.153 / 0.248 |
| Recovery, Small, edit | 9.510 / 9.792 / 10.105 | 10.022 / 10.317 / 10.464 |
| Recovery, Small, burst | 19.034 / 19.411 / 19.876 | 19.852 / 20.207 / 20.562 |
| Recovery, 100k lines, warm | 0.129 / 0.163 / 0.326 | 0.173 / 0.204 / 0.242 |
| Recovery, 100k lines, edit | 27.960 / 28.860 / 29.492 | 28.974 / 29.747 / 30.450 |
| Recovery, 100k lines, burst | 37.202 / 38.119 / 38.517 | 38.710 / 39.547 / 40.220 |
| Valid body, Small, warm | 0.131 / 0.139 / 0.286 | 0.143 / 0.161 / 0.234 |
| Valid body, Small, edit | 9.824 / 10.332 / 10.943 | 1.092 / 1.166 / 1.247 |
| Valid body, Small, burst | 19.378 / 20.245 / 21.081 | 10.930 / 11.306 / 11.382 |
| Valid body, 100k lines, warm | 0.128 / 0.162 / 0.308 | 0.142 / 0.175 / 0.223 |
| Valid body, 100k lines, edit | 28.043 / 30.303 / 31.249 | 1.207 / 1.305 / 1.389 |
| Valid body, 100k lines, burst | 40.486 / 44.199 / 45.366 | 11.062 / 11.243 / 13.170 |

Valid-body edit median falls about 89% in the small fixture and 96% in the large one; large bursts fall
about 73%. Malformed-expression recovery medians are 3.6–5.4% higher, and warm requests add roughly
0.01–0.04 ms. This change improves supported body edits; it does not eliminate full-check costs. The
response still contains 56 items and 6,020 bytes in these fixtures. Initial large-process RSS is about
79–81 MiB in both builds; the sustained samples below distinguish later allocator behavior.

The 600-second valid-body soak completed without a failed response. Its minute samples ended at 540 s,
after 3,768 edit/request cycles per build: follow-up RSS ranged **81.97–82.89 MiB**, baseline
**82.06–108.38 MiB**. RSS is resident memory, not a live-object count. Separate tests check expired parsed
trees, replaced body scopes and bounded query-cache ownership.

The later diagnostic-version change was also compared with its immediate baseline `f6027a6`:
100k-line warm median/p95 **0.167/0.215 → 0.167/0.212 ms**; valid edit **1.280/1.459 → 1.401/1.549 ms**;
20-edit burst **12.073/13.434 → 11.519/12.752 ms**. The separate 31-sample recovery median was
**31.345 → 31.161 ms**. These controls do not establish an improvement for every workload.

## Validation boundaries

Only the stated macOS IDE endpoints were exercised. Windows transport and filesystem paths have
host-side regressions but still need Windows runtime validation. Human acceptance of the complete
editing workflow remains separate from automated sandbox checks.

Incremental reuse covers supported body edits after a clean check; declaration changes and malformed
syntax still use full checking. Cancellation skips queued work, not an executing semantic check.
Dynamic receivers need a known type for member completion. The [editor guide](../../editors/completion.md)
describes completion behavior; broader language-server work remains in the [backlog](../backlog.md).
