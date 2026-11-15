# Completion follow-up: November 2026

Measured on 2026-11-15 on macOS arm64, using C++17 Release builds and Apple Clang 21.0.0.
The comparison binary is the merged first completion PR, revision `c38c55a`. The measured compiler
contents are committed in `dfc33c9`, and the production editor and probe in `a726bfc`; both builds embed
the same Godot declarations. The survey guard used for sustained IDE runs is recorded in `fd1b98c`.
The original IDE measurements use Rider RD-262.10968.170 with JDK 25, launched only in a separate
sandbox. The approved LSP4IJ 0.21.0 adapter adds IntelliJ IDEA's free unified distribution as the
Community target in revision `d6c1a6e`; adapter validation is described below. No IDE was downloaded. Android Studio is
outside the scope.

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

Plugin checks: 134 tests pass and one opt-in probe skips. Core: 82 pass and three opt-in probes skip.
Python: nine tests pass. Build and verifier succeed against both installed IDE endpoints. The regular editor probe
passes; light/dark screenshots were inspected. The control opens only plain text and confirms all three
projects close and are disposed. Both reproduce the same `HighlightingPanel` shutdown warning from
Rider's Problems View alarm. This control narrows the warning's source; it is not proof of zero IDE leaks.

Formatting and checks run separately: an earlier combined format/check invocation raced on the same
files and failed; the sequential rerun passed. Review regressions and an initial diagnostic-version
capability failure were fixed before the passing suites.

## IDEA and Rider adapter validation

The same artifact uses optional descriptors to select the client. IDEA is the outer condition around
LSP4IJ: Rider never gains that dependency's class-loader parent. A failing native navigation probe
exposed the earlier reversed nesting, which mixed LSP4J classes from two loaders. The regression now
checks real go-to-declaration with LSP4IJ installed in Rider, as well as descriptor isolation.

| Sandbox | Client | Result |
|---|---|---|
| IDEA 2026.1, IU-261.22158.277, free unified distribution | LSP4IJ 0.21.0 | Completion and language-feature assertions pass in three disposable projects |
| Same IDEA, LSP4IJ disabled | Local editor handlers | Pairing/indentation and one actionable install notification pass |
| Rider 2026.2.3.1, RD-262.10968.170, LSP4IJ installed | Native Rider LSP | Completion and language-feature assertions pass in three disposable projects |
| Same Rider, LSP4IJ disabled | Native Rider LSP | The same semantic assertions pass |

Each semantic run checks Enter insertion, Tab replacement, continued typing and Backspace while the
lookup is open, snippets and argument selection, prefix refresh, auto-imports, override stubs and atomic
Undo. Real IDE actions check parameter help and go to declaration; the outline must include the method.
All fixture projects close and report disposed. These short runs are functional checks, not sustained
memory measurements. Light/dark panel images were inspected. The ZIP contains no UI probe classes or
bundled LSP4IJ/LSP4J JARs. Python's nine tests also pass.

Fail-first regressions cover descriptor isolation, missing-dependency guidance, Enter/Tab ranges,
position copying after lookup typing, prefix refresh and diagnostic-version negotiation. The adapter
uses public completion and lookup-listener hooks; it keeps the client's insertion, snippets, additional
edits and Undo. It advertises LSP4IJ's existing diagnostic-version filtering. Verifier reports no
compatibility failures or internal API usages at either endpoint; existing experimental platform APIs
are reported without a suppression.

Run the four sandbox tasks sequentially:

```sh
./gradlew --offline runIdeaWithoutLsp4ij -PuiProbe
./gradlew --offline runIdea -PuiProbe
./gradlew --offline runIde -PuiProbe
./gradlew --offline runRiderWithoutLsp4ij -PuiProbe
```

The [contributor guide](../../editors/jetbrains/CONTRIBUTING.md#language-client-compatibility-checks)
locates the reports and describes readiness checks and sandbox-only dialog handling. IDEA initialization
must pass LSP4IJ's indexing gate before the first lookup; merely leaving IDEA's dumb mode is insufficient.
An initial probe race was fixed by checking that gate and invoking completion in one UI-thread turn.
The parameter-help probe waits for the live template to finish, then asks about the completed call and
reactivates the sandbox before showing its hint. An intermediate Rider run that shrank the entire
document immediately after completion intermittently failed parameter help; that separate sequence
remains unresolved. Passing the normal post-completion flow does not establish that it was fixed.

LSP4IJ reports `Read end dead` during project shutdown in these probes. Its process listener logs any
broken stdout pipe as a crash, including queued output after its reader stops. That warning alone does
not establish a compiler crash. Rapid whole-document replacement also exposed stale diagnostic ranges:
LSP4IJ catches an invalid-range exception and logs a warning; Rider logged an uncaught
`IndexOutOfBoundsException` at SEVERE level in its native diagnostic cache after the document shrank.
Version negotiation rejects mismatched publications but does not prove that every previously accepted
diagnostic is invalidated before rendering. These client issues remain unresolved and tracked in LS-2;
passing completion assertions do not establish error-free diagnostic handling or leak-free IDE behavior.

## Final integration check

Revision `33defc4` includes the current main compiler stages from `044728d`. The merge needed only
backlog/roadmap conflict resolution. Release passes all 24 groups in 20.87 seconds; ASan/UBSan passes
all 24 in 155.43 seconds. The separately rebuilt extension reports `godot: OK`. An initial integration
run used the older extension binary and failed its Godot group; rebuilding the matching extension
before rerunning CTest resolved that build-state error.

The final artifact passes `check buildPlugin verifyPlugin`, all four sandbox configurations, and nine
Python tests. JUnit reports 134 plugin passes with one opt-in skip, and 82 core passes with three opt-in
skips. Both verifier endpoints report compatibility, four deprecated API uses and 26 experimental API
uses; there are no internal API usages. The opposite product's optional module is absent as intended.
Packaged compiler/extension hashes match the integration builds. No sandbox `kas3c` process remained
after the matrix, and the final light/dark images were inspected.

The integration review found no remaining code blocker and corrected documentation counts to 136 backlog
items. It did not rerun the suites independently. The subsequent Agy review found the override defects
described below. These integration runs validate functionality; the
performance and sustained-retention measurements below remain tied to their stated earlier revisions.

## Override review fixes

Revision `a11c2ba` fixes two defects reproduced after Agy reviewed the PR:

- An inherited method with `Base.VALUE` or `a.b.Base.VALUE` as a default was omitted. Completion now
  distinguishes a resolved class member from a package-qualified definition and checks both for
  accessibility. Package prefixes are not treated as unresolved value references.
- A getter override hid its complementary setter, and a final accessor declared second could be offered.
  Completion now checks the selected accessor's declaration and modifiers. It ignores invisible nearer
  members before recording shadowing, while visible fields still shadow inherited methods.

The new `test_override_completion` group first reported `54 checks, 10 failed` against the old code.
It now reports `96 checks, 0 failed`. Cases include all three constant spellings, Godot constants,
private/inaccessible references, either accessor order, partial inherited properties, empty/partial/full
names and ordinary methods named `get`. Accepted stubs are applied and checked in a fresh compilation.
The empty-name cases also required parser recovery to retain the unfinished accessor's source span.
Candidate-only shadowing checks deliberately exercise invalid-source recovery.

After the fixes, Release passes all 25 CTest groups in 19.05 seconds, ASan/UBSan all 25 in 116.13 seconds,
and the rebuilt extension reports `godot: OK`. `check buildPlugin verifyPlugin` passes at both installed
IDE endpoints. JUnit totals remain 134 plugin passes with one opt-in skip and 82 core passes with three
opt-in skips; nine Python tests pass. Packaged compiler and extension hashes match the rebuilt binaries,
and the distribution contains no probe classes or bundled LSP4J JARs. Light/dark images were inspected.

Rider with LSP4IJ installed and IDEA with/without it pass their sandbox checks. Rider without LSP4IJ
timed out twice waiting for the first lookup in the continued-typing case after document replacement.
A subsequent run passed all three project cycles; no code changed between these runs. This unchanged rerun did not establish a fix. The later focus investigation below explains the
timeout. Earlier native diagnostic and
disposal warnings remain. The test logs are `agy-fixes-*.log` in each ignored build directory. These
correctness checks do not replace the earlier latency or ten-minute retention measurements.

Agy re-reviewed `b8d3048..2337414`, confirmed both findings resolved and found no new actionable
regression. It independently ran the new override group in Release and ASan/UBSan (96 checks each),
`test_query` (42,249 checks) and the style check; all passed. It inspected the broader recorded results
without rerunning those suites. Its non-blocking note identified incomplete empty-name recovery before
another member. A separate stdio check confirmed `override function get |` offers `count` at the end of
the class, omits it before a following public method, and offers it again after typing `c`. The guide
stated that limit, which is fixed in the recovery change below. This note did not affect the fixed
qualified defaults or accessor-slot filtering.

## Recovery and background sandbox focus

Revision `143b55a` preserves empty override names before a following class member. Recovery recognizes
next-line declarations without consuming their modifiers as names. It retains following metadata and
continues accepting valid multiline method/accessor signatures, including names such as `get`, `set`,
`override` and reserved accessor names. Completion cases cover methods, getters and setters before
fields, methods, modifiers and metadata, with LF, CRLF and comments. They apply the offered edits and
fresh-check the result. Parser cases also inspect the recovered tree.

The completion regressions first reported `270 checks, 57 failed`; the metadata-retention parser cases
then reported `503 checks, 6 failed`. After both changes, the groups report `384 checks, 0 failed` and
`503 checks, 0 failed`. Release passes all 25 groups in 20.74 seconds and ASan/UBSan all 25 in 250.75
seconds; the separately rebuilt Godot extension reports `godot: OK`. These are correctness runs under
concurrent build load, not performance comparisons.

The Rider trace showed a valid `normalized` response for the request that timed out. A failed probe's
lookup-disposal stack then identified `IdePopupManager` handling window deactivation through
`maybeCloseAllPopups` and `LookupImpl.onPopupCancel`. The background sandbox had lost its popup; the
server was not stuck. The probe reproduced the timeout before the focus fix, including with added
focus-event checks. It also reproduced cancellation at the preceding latest-document lookup.

Revision `21428d8` scopes the platform's `skipWindowDeactivationEvents` test control to the sandbox
completion run. It saves and restores the previous value on the UI thread in `finally`. Controlled
window-deactivated and lost-focus events go through the real popup manager while lookup is open;
continued typing, Backspace, Enter/Tab and Undo still use native handlers. The control and checks are
excluded from the distributable ZIP. This fixes the observed background-probe cancellation; it does
not change or establish production OS-focus behavior, resolve stale diagnostic ranges, or prove that
all possible lookup delays are gone.

The offline `check buildPlugin verifyPlugin` passes. JUnit totals are 134 plugin passes with one opt-in
skip and 82 core passes with three opt-in skips. The packaged compiler and extension match the rebuilt
binaries, and no probe classes appear in the distribution. The existing deprecated/experimental API
reports remain at both installed verifier endpoints. Five consecutive native-only Rider runs pass all
15 project cycles. Rider with LSP4IJ installed and IDEA with/without it also pass; nine Python tests
pass. Light/dark images were inspected. Logs and per-run reports are `recovery-final-*` under the
ignored JetBrains build directory; compiler logs are `recovery-*.log` in their build directories.
The original server benchmarks and ten-minute
retention measurements were not repeated for these correctness fixes.

Agy reviewed `b4a443e..ba9f26c` and reported no actionable findings. It confirmed both fixes and their
documented scope, independently passed the override group (384 checks), parser group in Release and
ASan/UBSan (503 checks each), and style check, and verified binary matching and probe exclusion. It
inspected the broader compiler, Gradle and sandbox results from logs rather than rerunning those suites.

## Literal braces in generated overrides

Manual sandbox testing found a literal backslash before the closing brace of
`setPropertyIsEnumerable`. The server escaped every snippet closing brace. The installed Rider
`SnippetToTemplateConverter` replaces template variables but retains that escape in ordinary text.
The server now escapes `}` only inside placeholder text; dollar signs and backslashes retain their
existing escaping. No client-specific protocol flag or production editor handler was added.
Literal top-level braces follow the [documented snippet examples and grammar](https://code.visualstudio.com/docs/editing/userdefinedsnippets#_grammar).

Before the fix, the new in-process check reported `109160 checks, 1 failed`, the stdio check failed
its exact CRLF override text assertion, and the native Rider probe reproduced the unwanted backslash.
The probe previously checked only signature/body substrings. It now compares the complete document's
non-whitespace text, checks the caret before `throw`, and verifies atomic Undo. Both `_physicsProcess`
and the built-in `setPropertyIsEnumerable` are covered. Wire tests also cover a client without snippets.

After the fix, `test_lsp` reports `109160 checks, 0 failed`, and `lsp stdio: OK`. Release passes all
25 groups in 19.56 seconds; ASan/UBSan passes all 25 in 181.47 seconds. The separate engine runner
reports `godot: OK`. Rider's native client with LSP4IJ installed and IDEA with LSP4IJ each pass three
project cycles. Gradle `check buildPlugin verifyPlugin` passes against both installed endpoints.
JUnit reports 134 plugin tests passed/one
skipped and 82 core tests passed/three skipped; unchanged core results were reused by Gradle. Python's
nine tests pass. Light/dark panel images were inspected. The ZIP's compiler matches the rebuilt binary,
and the ZIP contains no probe classes.

The native-only Rider sandbox remained open during the automated checks. The owner then approved
restarting it to load the new binary. The server initialized successfully, and saved AS3 files were
unchanged across the restart; previously inserted source is not rewritten.
Logs and preserved reports use `snippet-*` in the ignored build
directories. No new latency or retention measurement was made for this serialization fix. The earlier
diagnostic-range, lifecycle and platform-coverage limits remain.

Agy reviewed the fix delta from `7ccf5f1` and reported no actionable findings. It independently ran the
style check and inspected the fail-first evidence, Release, engine, Gradle, Python and both IDE results.
The sanitizer run was still in progress when inspected; its completed result above was verified
separately after the review's inspection.

## AS3 language-block templates

Accepting `try` now inserts a try/catch block. The same query path supplies `if`, `while`, `do/while`,
`for`, `for each` and `switch` blocks. Editable expressions, names and bodies have numbered tab stops;
repeated names share an index, and zero exits the template. The default catch rethrows its error.
The front end stores ordinary AS3 text and byte spans; the LSP serializer alone encodes snippet syntax.
Override stubs use the same span representation and retain their previous insertion text.

Eligibility uses cached AST containers and parsed comment intervals. It excludes expressions and
loop headers, including a fully typed keyword recovered as a body before a missing closing parenthesis.
An existing `(` or `{` after the keyword suppresses block expansion. Empty gaps use conservative
brace/semicolon boundaries; after an automatic-semicolon-insertion gap, typing a prefix identifies
the statement. Empty function-prefix lists refresh accordingly. `for each` is a choice while typing
`for`. Declaration templates and additional block variants are outside this change. Clients without
snippet support keep keyword insertion.

Fail-first evidence: the initial query group reported `82 checks, 24 failed`, the stdio group failed
seven assertions, and Rider acceptance inserted only `try`. Added empty-expression checks failed three
assertions before the boundary guard; fully typed `try` in unfinished `if` and `for` headers failed two
before the backward header check. The final focused group reports `295 checks, 0 failed`; generated
defaults fresh-check successfully, tab-stop spans are ordered and in bounds, and mirrored defaults
agree. The stdio group reports `lsp stdio: OK` across independent snippet/range-default capabilities.

Rider and IDEA each passed three project cycles with all seven blocks, complete non-whitespace text
comparison, immediate insertion Undo, Tab navigation and final caret checks, and catch-name mirroring.
Rider exposes raw snippet text as the lookup string; LSP4IJ exposes the label. The probe handles both.
IDEA indents blank lines differently, so body-caret checks use the line boundary. Native Rider records
template-navigation Undo steps: Undo after tabbing can first restore a caret state without changing
source. Immediate insertion Undo is tested separately. No production Undo handler was introduced.

Release passes all 26 groups in 21.22 seconds and ASan/UBSan all 26 in 211.37 seconds; the rebuilt
engine runner reports `godot: OK`. Offline Gradle `check buildPlugin verifyPlugin` passes, with 134
plugin tests passed/one skipped and 82 core tests passed/three skipped; unchanged core results were
reused. Python's nine tests pass. Final Rider and IDEA probes each pass three project cycles, including
the first-selection and explicit template-exit checks. Light/dark panel images were inspected. Both
verifier endpoints remain compatible with four deprecated and 26 experimental API usages. Packaged
compiler/extension hashes match the builds, and no probe classes are shipped.

Agy's first review identified the probe's Undo sequencing and missing exit wait. Both were corrected.
Its re-review found no actionable findings, including in the updated header-recovery guard. It ran
the style check independently and inspected recorded tests. The final IDEA run completed after its
inspection; that passing result was verified separately.

Logs and preserved reports use `language-*` under the ignored compiler and JetBrains build directories.
The existing diagnostic-range, retention and platform-coverage limits remain unchanged.
After the owner saved and approved a restart, the native-only manual Rider sandbox initialized the
rebuilt server and reopened the scratch project. Saved AS3 files were unchanged across the restart.
Automated acceptance above covers Rider and IDEA; the new templates were not exercised in VS Code's UI.

### Language-template performance controls

The existing member-completion driver compared the saved `f9ead1e` executable with this change,
alternating builds. This checks the previous completion path, not keyword-template acceptance latency.
The owner's manual sandbox remained open; no build or automated IDE probe ran during measurement.
These shared-host controls are separate from the original controlled measurements below.

```sh
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build/language-baseline --runs 31 --valid-body
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build/language-baseline --runs 31
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build/language-baseline --runs 101 --valid-body
```

The 31-sample valid-body run showed substantial scheduling variation: on 100k lines, median edit plus
completion was 1.900 → 2.092 ms and the 20-edit burst was 20.587 → 19.679 ms. A 101-sample follow-up,
after the review process exited, gave the following controls:

| 100k-line workload | Before median / p95 (ms) | After median / p95 (ms) |
|---|---:|---:|
| Warm member completion | 0.254 / 0.339 | 0.249 / 0.340 |
| Valid body edit + completion | 1.666 / 1.810 | 1.726 / 1.930 |
| Twenty valid body edits + completion | 15.109 / 27.862 | 15.807 / 26.233 |

Valid-edit and burst medians are 3.6% and 4.6% higher in that follow-up; this change makes no speedup
claim. In the 31-sample malformed-source run, median edit plus completion was 50.031 → 49.374 ms.
Every control returned the same 56 member candidates and 6,020 response bytes. Cold RSS varied across
runs: the 101-sample 100k-line run started at 92,096 → 89,424 KiB, while the earlier valid-body run was
91,248 → 94,096 KiB. Those single startup samples are not a retention or leak measurement. No new
ten-minute soak was run. Full output is in `compiler/build/language-bench-*.log`.

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

- Compatibility is verified at IDEA IU-261.22158.277 and Rider RD-262.10968.170 on macOS arm64.
  Other builds inside the descriptor range and the older standalone Community distribution are unverified.
  IDEA requires LSP4IJ 0.21.0; Rider uses its native client.
- Windows binary I/O, pipe readiness and UTF-8 filesystem branches have not run on Windows. Host-side
  path fixtures do not establish Windows compatibility. Case/symlink/Unicode-normalization identity
  remains separate work.
- Incremental reuse applies only after a clean full check and only to supported body changes. Syntax
  errors, declaration changes, accessors, interfaces and function-local imports use the full checker.
- Cancellation affects queued requests, not a semantic check already executing. No concurrent mutation
  of compiler syntax trees was introduced.
- Override generation requires an unfinished method/accessor name before its parameter list. It skips
  inaccessible signature types and non-public member defaults, preserves existing metadata/modifiers,
  and emits an explicit throwing body for the user to implement. Empty names work before a following
  member declaration, including its metadata and modifiers.
- The pinned Rider retains all ten disposed projects in both sustained workloads and reports a
  Problems View disposal warning. The exact GC root is not verified. Manual editing acceptance and
  broad IDE/platform compatibility remain unverified.
