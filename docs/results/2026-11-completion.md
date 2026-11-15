# Completion and typing assistance: November 2026

Measured on 2026-11-15, on macOS 27.2.1, arm64, with Apple Clang 21.0.0
(`clang-2100.3.34.2`), C++17 Release builds. The comparison binary was built from unchanged revision
`b491adb` before implementation. Both binaries use the same embedded Godot declarations. The IDE
probe uses the installed Rider RD-262.10968.170 and its JDK 25, in a separate Gradle sandbox.
Existing declared Gradle dependency downloads were approved; no new dependency was added.

## Observed behavior

The original sandbox failed to pair all five opening punctuation characters: `(`, `[`, `{`, `'` and
`"`. The native brace matcher expected AS3 tokens while the editor highlighter supplied TextMate tokens.
The adapter now supplies native token kinds and preserves the TextMate color attributes.

The sandbox passes pairing and closing-character skip, empty-pair backspace, pairing disabled in
settings, two carets, suppression in comments and regex literals, and repeated Enter inside a block.
The regex tests include a control condition followed by a literal. The local lexer still has an
ambiguous closing-brace case, recorded in the backlog and user guide.

A real native LSP lookup in three disposable projects offers `normalized` at
`transform.origin.norm|suffix`. Tab replaces the whole identifier and inserts `normalized()`.
Enter inserts `normalized()` and preserves `suffix`, matching Rider's separate insertion action.
Use Tab when replacing a suffix. A required argument is selected after insertion, and Tab moves past the call. Undo restores the original identifier. Changing the document while
a lookup is pending cancels the old lookup and offers members for the latest prefix. These are
automated native action tests, not a person's acceptance. The probe delivers a window-activation
event to its own sandbox frame so background launches can show hints. It does not test OS focus
activation or remap production keys.

The independent implementation review found five defects: Unicode whitespace included in replacement
ranges, indentation increasing on blank lines, comments between a callee and `(` disabling signature
help, an empty import suggestion, and regex after control conditions classified as code. Each received
a failing regression and a fix. The second review found no new blocking defect and independently ran
`test_query`, `test_lsp`, `test_workspace` and `lsp_stdio`: four passed in 17.25 seconds.

## Server latency

Commands, from the repository root:

```sh
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-baseline --runs 101
python3 compiler/tests/bench/run_bench.py compiler/build --against compiler/build-baseline --runs 21
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-baseline --runs 101 --seconds 600
```

The first two commands were measured without concurrent builds. Completion measurements alternate
which binary runs first; the existing check/run controls interleave the binaries in a fixed order. The edited file has 5,003 lines, mostly comments, ending in a typed member chain; this
exposes the old repeated lexical scan. The large project adds 100,000 generated lines. Both load Godot.
The numbers below include stdio transport and Python JSON decoding, not IDE rendering.

| Project and request | Before median / p95 / max (ms) | After median / p95 / max (ms) |
|---|---:|---:|
| Small, unchanged document | 0.240 / 0.287 / 0.465 | 0.132 / 0.182 / 0.377 |
| Small, edit then request | 9.959 / 10.543 / 17.729 | 9.946 / 10.522 / 12.221 |
| Small, twenty-change burst | 19.264 / 20.798 / 37.268 | 19.374 / 20.981 / 30.478 |
| 100k lines, unchanged document | 0.253 / 0.283 / 0.431 | 0.139 / 0.181 / 0.235 |
| 100k lines, edit then request | 29.295 / 30.283 / 34.268 | 29.598 / 30.613 / 41.224 |
| 100k lines, twenty-change burst | 38.376 / 39.502 / 43.612 | 38.790 / 40.297 / 79.637 |

Warm completion median is about 45% lower on this fixture. Edit latency is essentially unchanged:
whole-project name/type analysis remains the dominant cost. The larger burst maximum regressed;
median and p95 differ by about 1–2%. These measurements do not establish a general improvement for
all project shapes or eliminate long-tail scheduling delays.

Cold initialization, opening and first completion were 47.728 -> 43.068 ms for the small project and
122.377 -> 125.140 ms for 100k lines. These are single cold samples, not distributions.
Each response has 56 candidates. Payload grows from 4,685 to 6,020 bytes because results now carry
sorting and edit metadata, with shared edit ranges negotiated by the client. Clients lacking shared
ranges receive larger per-item edits.

The existing check/run controls reported:

```text
check compiler/build              94.7 ms   spread    4.1 ms   (21 runs)
check compiler/build-baseline      91.0 ms   spread    5.7 ms   (21 runs)
check    the first is +4.1% against the second
run compiler/build               311.2 ms   spread   17.5 ms   (21 runs)
run compiler/build-baseline      306.2 ms   spread   40.0 ms   (21 runs)
run      the first is +1.6% against the second
```

The median differences are within the reported spread. Retaining source scopes adds work and memory;
this change does not claim to speed up ordinary compilation or execution.

## IDE latency and memory

The real editor's paired `(` action, after one warm-up, took 3.260 ms median, 4.458 ms p95 and
4.984 ms maximum across 100 samples. Document reset is outside the timed interval. This is synchronous
key handling, including editor work; it is separate from completion popup latency.

Across three project-open cycles, native lookup medians were 27.27, 21.21 and 22.62 ms; p95 values were
58.24, 37.40 and 30.18 ms. Each has twenty warm samples, with a 10 ms observation polling interval.
The first lookup after LSP initialization was 319.92 ms; later project cycles were 99.32 and 29.49 ms.
These UI timings exclude project loading and language-server initialization. Warm samples meet
the design's 100 ms target for this fixture. Cold startup does not. There is no before/after IDE
latency comparison, and no claim that every project meets the target.

The ten-minute alternating edit/request run completed 3,465 cycles after its timing samples. After
settling, baseline RSS was 111,632 KiB and changed RSS was 116,016 KiB (about 4.3 MiB more). Changed
RSS stayed at 116,016 KiB from one minute through ten minutes. This soak preceded the final Unicode
boundary fixes and allocation-only cleanup; the fixture is ASCII. Concurrent builds affected its
latencies, so its timings are not used in the latency table.

RSS is not a leak test. `test_workspace` separately checks forty compilation generations, each with
100 repeated requests: one candidate cache entry and one context index in the current generation,
zero in the replacement generation before use, and zero after closing the document. No prefix-response
history or old AST pointers are retained by the completion caches.

Whole-IDE used heap sampled after explicit GC and project close was 282,885,152, 288,022,368 and
291,456,256 bytes. It increased by about 8.2 MiB across these cycles. This includes Rider's own caches
and startup services; it does not isolate completion retention or prove the IDE leak-free. The final sandbox also logged a retained
`HighlightingPanel` under the platform's `ROOT_DISPOSABLE` during rapid project closure. Its stack is
in Rider's Problems view; ownership has not been isolated beyond that observation. This warning and a
sustained Rider heap profile remain follow-up work, recorded in the backlog.

## Verification

The regression history includes failures before the implementation: lexical scope/import/regex queries,
LSP edits and signatures, stdio requests, native pairing, and the five review regressions. All final
outputs and timing logs are in ignored build directories.

```sh
cmake --build compiler/build -j8
ctest --test-dir compiler/build -j8 --output-on-failure
cmake --build compiler/build-godot -j8
python3 compiler/tests/run_godot.py compiler/tests
cmake --build compiler/build-san -j8
ctest --test-dir compiler/build-san -j4 --output-on-failure
cd editors/jetbrains
./gradlew --offline check buildPlugin verifyPlugin
python3 -m unittest discover -s tests -v
./gradlew --offline runIde -PuiProbe
```

Release (23.52 s) and ASan/UBSan (130.80 s) suites each reported
`100% tests passed out of 24`; the extended cache-lifetime test then passed 16,161 checks in both builds.
The separate Godot runner
reported `godot: OK`. The sandbox reported `passed`, including the existing panel controls. Light and
dark panel screenshots were inspected. The plugin check and ZIP build passed: 110 plugin tests and 48 core tests, with zero failures or skips.
All seven Python client tests passed. The verifier reported compatibility with RD-262.10968.170. Its
experimental highlighter attribute-key API warnings are retained deliberately to forward TextMate
colors faithfully; compatibility is limited to the pinned IDE build. Existing tool-window API
warnings are unrelated to completion.

Windows, other IDE versions, manual Rider acceptance, and a sustained whole-IDE heap profile were not
verified. Android Studio and IntelliJ Community do not supply the LSP module this plugin requires.

## Integration before merge

The completion branch was integrated with the latest main branch on 2026-11-15, preserving native
Simulator work and Rider's hover language identifier. The README test-count conflict was resolved
against the combined test results. Independent source review found no blocking integration defect.

The first Release test run started before the updated Godot extension was rebuilt and failed its
Godot group against the old binary. After rebuilding that extension, the separate Godot runner
reported `godot: OK`, the full Release suite passed 24/24 in 22.44 seconds, and the full ASan/UBSan
suite passed 24/24 in 126.80 seconds. The merged plugin's offline check, ZIP build and verifier passed:
119 plugin tests passed with one opt-in probe skipped; 82 core tests passed with three opt-in probes
skipped. All nine Python tests passed. The sandbox action probe passed in 31 seconds, and light/dark
panel screenshots were inspected. The previously recorded platform `HighlightingPanel` shutdown
warning remains and is part of the next completion follow-up investigation.
