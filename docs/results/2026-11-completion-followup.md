# Completion follow-up: November 2026

Measured on 2026-11-15 on macOS arm64, using C++17 Release builds and Apple Clang 21.0.0.
The comparison binary is the merged first completion PR, revision `c38c55a`. The measured compiler
contents are committed in `dfc33c9`, and the production editor and probe in `a726bfc`; both builds embed
the same Godot declarations. The survey guard used for sustained IDE runs is recorded in `fd1b98c`.
The IDE is Rider RD-262.10968.170 with JDK 25, launched only in a separate sandbox.
Rider and IntelliJ IDEA Community are the intended targets. Community integration is pending dependency
approval and validation. Android Studio is outside the scope. No new dependency was added.

## What was verified

- Compiler queries apply automatic imports and generated override edits, then check the resulting source
  with a fresh compiler. Cases include CRLF, helpers outside the package, qualified types/defaults,
  metadata, existing modifiers, visibility, existing signatures and incomplete type positions.
- Body-only rechecks match fresh full checks for diagnostics, names and types. Repeated edits release old
  parsed trees and replace body scopes/symbols. Moving function imports or changing a signature falls
  back; cross-file batches resolve every changed unit before typing any of them.
- The protocol rejects stale versions, carries negotiated diagnostic versions and cancels obsolete queued
  work. Tests send complete requests followed by partial headers/bodies and require a response before
  the following frame is completed. Malformed lookahead messages and invalid versions cannot cancel a
  valid request.
- Local lexer tests cover braces in expression/statement contexts, control parentheses, function forms,
  object keys that are keywords, ternaries, labels and lexer restarts. Compiler discovery selects `.exe`
  on Windows. Network-share URIs and POSIX backslashes have host-side regressions.
- Rider's native actions accept auto-import and override edits, and one Undo restores all changed text.
  Existing tests also pass Tab replacement, Enter insertion, chained members, argument placeholders,
  changed-document lookup cancellation, typing into an empty-prefix lookup, punctuation pairing and
  indentation. These are automated editor checks; manual acceptance is still pending.

The independent review found and verified fixes for import-sensitive incremental signatures, batch
resolution order, generated-edit ranges, inaccessible signature/default references, metadata loss,
helper source spelling, fragmented-transport blocking, version validation and Unicode filesystem
conversions. The host-tested behavioral fixes have regressions observed failing before the fixes. Windows-only
filesystem branches remain unverified on their target platform. The last read-only
review found no remaining blocker; it did not independently run the full build suites.

## Commands and checks

```sh
cmake --build compiler/build -j8
cmake --build compiler/build-godot -j8
ctest --test-dir compiler/build -j8 --output-on-failure
python3 compiler/tests/run_godot.py compiler/tests
cmake --build compiler/build-san -j8
ctest --test-dir compiler/build-san -j4 --output-on-failure
```

Release: all 24 groups pass; the final transport refinement completed in 20.48 seconds. The separate
Godot runner reports `godot: OK`. ASan/UBSan: all 24 groups pass in 142.66 seconds after the final transport refinement.

From `editors/jetbrains`, with `JAVA_HOME` set to the installed JDK 25:

```sh
./gradlew --offline check buildPlugin verifyPlugin
python3 -m unittest discover -s tests -v
./gradlew --offline runIde -PuiProbe
KUMIKAE_COMPLETION_CONTROL_PROBE=1 ./gradlew --offline runIde -PuiProbe
```

Plugin checks: 126 tests pass and one opt-in probe skips. Core: 82 pass and three opt-in probes skip.
Python: nine tests pass. Build and verifier succeed against the pinned Rider. The regular editor probe
passes; light/dark screenshots were inspected. The control opens only plain text and confirms all three
projects close and are disposed. Both reproduce the same `HighlightingPanel` shutdown warning from
Rider's Problems View alarm. This control narrows the warning's source; it is not proof of zero IDE leaks.

Formatting and checks run separately: an earlier combined format/check invocation raced on the same
files and failed; the sequential rerun passed. Review regressions and an initial diagnostic-version
capability failure were fixed before the passing suites.

## Timing and retention method

```sh
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-completion-baseline --runs 101
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-completion-baseline --runs 101 --seconds 600 --valid-body
python3 compiler/tests/bench/run_bench.py compiler/build --against compiler/build-completion-baseline --runs 21
```

Run after builds and sandbox sessions finish. Each completion comparison alternates which binary runs
first. The check/run controls interleave binaries in fixed order. Both receive the same 5,003-line file,
with Godot loaded; the large fixture adds 100,000 generated lines. Timings include stdio and Python JSON
decoding. Cold values are single observations, not a statistically established startup comparison.

The default fixture ends in an incomplete member expression and measures full-check recovery.
`--valid-body` completes at the same dot in a valid expression, then changes a comment inside its body.
These are separate workloads. An initial run exposed duplicate JSON decoding in the request queue;
that work was removed before the final comparison. The empty-prefix client-caching fix also preceded
the final measurements. Exploratory interrupted soaks are not used as evidence.

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

Cold initialization plus first completion, single observation in milliseconds:

| Fixture | Merged baseline | Follow-up |
|---|---:|---:|
| Recovery, Small | 49.504 | 51.473 |
| Recovery, 100k lines | 122.547 | 123.225 |
| Valid body, Small | 43.274 | 45.468 |
| Valid body, 100k lines | 121.502 | 124.553 |

### Server retention and compiler controls

The valid-body comparison completed its 600-second soak without a failed response. RSS is process
resident pages, not live-object counts. Samples were taken every minute; the last scheduled sample was
at 540 seconds, after 3,768 soak edit/request cycles per build. The driver does not emit a final sample
at the 600-second boundary.

| Seconds | Baseline RSS (MiB) | Follow-up RSS (MiB) |
|---:|---:|---:|
| 0 | 82.06 | 81.97 |
| 60 | 85.78 | 81.97 |
| 120 | 85.91 | 82.73 |
| 180 | 86.00 | 82.73 |
| 240 | 86.17 | 82.73 |
| 300 | 95.91 | 82.75 |
| 360 | 95.91 | 82.75 |
| 420 | 96.03 | 82.89 |
| 480 | 96.03 | 82.89 |
| 540 | 108.38 | 82.89 |

Follow-up RSS ranged from 81.97 to 82.89 MiB in these samples; baseline RSS ranged from 82.06 to
108.38 MiB. These observations do not prove absence of leaks. Separate workspace tests verify expired
parse snapshots, replaced body symbols/scopes and bounded query-cache ownership.

Interleaved controls, 21 runs per build:

| Workload | Baseline median / spread (ms) | Follow-up median / spread (ms) |
|---|---:|---:|
| Full check, 100k generated lines | 94.1 / 4.9 | 95.2 / 6.1 |
| Interpreter loop | 302.3 / 11.2 | 308.8 / 18.9 |

Medians are 1.2% and 2.2% higher, respectively; each difference is smaller than its observed spread.
No concurrent build or sandbox IDE ran during these latency/control measurements.

The optional IDE retention command is:

```sh
KUMIKAE_COMPLETION_RETENTION_PROBE=1 ./gradlew --offline runIde -PuiProbe
```

It opens, exercises and closes ten temporary projects across ten minutes. Live class histograms force
collection after each closure and at the end; weak references count closed projects still reachable.
Histograms and source-bearing traces remain in ignored build output. Three quick heap samples alone
are not a sustained-retention result.

### IDE lifecycle investigation

An initial sustained run stopped after two samples because Rider opened an unsolicited evaluation
feedback survey. A thread dump showed the application starter waiting in project opening while that
modal dialog was active. That run was terminated and is not a ten-minute result. The test-only guard
cancels exactly `LongEvaluationFeedbackDialog` with its cancel exit code; it does not submit feedback,
handle license dialogs or change IDE settings. The rerun recorded the cancellation and continued.

Installed SDK inspection identifies a possible strong ownership path:

`Disposer ObjectNode.myObject → HighlightingPanel → ProblemsViewPanel.mySession → ClientProjectSessionImpl.project`

The first two live histograms showed panel counts rising from one to two alongside retained-project
counts. The plain-text probe reproduces the panel disposal warning. These observations support a
platform lifecycle problem in this sandbox, but no live heap-reference traversal was performed to
establish every root or exclude other retention paths.
The probe itself stores weak project references; synchronous project/editor locals have returned before
collection.

Both ten-minute runs completed and their functional assertions passed. AS3 measured 600.21 seconds;
the plain-text control measured 600.34 seconds. Each confirmed ten successful closures and disposals,
but all ten weak project references remained live after the final full collection.

| Sample | AS3 heap after GC (MiB) | Plain-text heap after GC (MiB) | Retained projects in each |
|---|---:|---:|---:|
| First project | 264.42 | 281.00 | 1 |
| Fifth project | 291.17 | 362.04 | 5 |
| Tenth project | 312.12 | 382.13 | 10 |
| Final, 600 seconds | 312.01 | 381.99 | 10 |

These are separate JVM runs in the same sandbox configuration. Their different startup/cache workloads
make subtracting heap totals inappropriate. In both final live histograms, `ProjectImpl` and
`HighlightingPanel` have ten instances. The AS3 run also retains ten native AS3 LSP descriptors; the
plain-text run has none. No `As3ProjectLanguageService` or `DeclarationRefresh` instance appears in any
post-close AS3 histogram. This does not exclude other plugin retention paths or identify the live root.
Both runs emit the Problems View disposal warning on shutdown. Retention remains unresolved; the
passing functional result is not a passing leak check.

## Remaining limits

- Community's optional LSP4IJ adapter and sandbox validation await owner approval for the new dependency
  and any required IDE download. The current plugin still requires Rider's native LSP module.
- Windows binary I/O, pipe readiness and UTF-8 filesystem branches have not run on Windows. Host-side
  path fixtures do not establish Windows compatibility. Case/symlink/Unicode-normalization identity
  remains separate work.
- Incremental reuse applies only after a clean full check and only to supported body changes. Syntax
  errors, declaration changes, accessors, interfaces and function-local imports use the full checker.
- Cancellation affects queued requests, not a semantic check already executing. No concurrent mutation
  of compiler syntax trees was introduced.
- Override generation requires an unfinished method/accessor name before its parameter list. It skips
  inaccessible signature types and non-public member defaults, preserves existing metadata/modifiers,
  and emits an explicit throwing body for the user to implement.
- The pinned Rider retains all ten disposed projects in both sustained workloads and reports a
  Problems View disposal warning. The exact GC root is not verified. Manual editing acceptance and
  broad IDE/platform compatibility remain unverified.
