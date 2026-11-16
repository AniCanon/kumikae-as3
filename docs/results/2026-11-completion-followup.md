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

The two findings that blocked the previous handoff are fixed: the LSP4IJ client judges a publication
with public API only, and the publication probe gives the server's own diagnostics back instead of
clearing them. On 2026-11-16 `verifyPlugin` found both installed builds compatible, each with **0**
internal, **6** deprecated, **45** experimental and **1** scheduled-for-removal API usages. Plugin tests
**174 passed** (1 opt-in skipped), core tests **82 passed** (3 opt-in skipped), ktlint clean, no compiler
warnings. The four sandbox probes passed: `runIdeaWithoutLsp4ij`, `runIdea`, `runIde` and
`runRiderWithoutLsp4ij`, each with completion, parameter help, hover documentation (six checks per run,
the server's signature in its code block and its owner), navigation, outline and diagnostics where a
client is enabled. `runIdea` passed six runs in a row, and the opt-in `KUMIKAE_COMPLETION_RETENTION_CHECK`
runs of `runIde` and `runIdea` passed (0 closed projects retained), after five causes of failure in the
probes themselves were fixed:

- Parameter help (1 failure in 11 `runIdea` runs): a hint left from an earlier request stayed visible,
  owned by no offset, so the wait passed at once; looking the new controller up before its hint showed
  disposed it. Hints are now closed before asking (no failure in 20 runs, then 12, then 6).
- Rider reopen ("Required value was null"): the probe copied the layout before it recorded the shown
  Problems window; it now waits for that record.
- Concurrent projects (no lookup in the reused frame): a short dumb mode cancelled a completion begun
  just before it, and no request reached the server (trace and IDE log). The probe asks again after three
  seconds with no lookup, and records it ("completion asked 2 times"; seen twice in ten runs).
- IDEA statement templates (no lookup, 1 in 20): LSP4IJ sends a committed edit from a callback, on another
  path than requests, so the completion request reached the server before the edit (trace). The probe sends
  LSP4IJ's pending edits before each request. A person's typing leaves this window open for milliseconds.
- IDEA retention (1 of 3 projects retained, also in the plain-text control): the only strong path was
  Swing's `BufferStrategyPaintManager.rootJ`, the last frame painted, found in a heap dump. The check paints
  a one-pixel window of its own first.

The stock Rider endurance result below predates these changes.
The previous compiler validation remains applicable: Release **26/26**, ASan/UBSan **26/26**, and
`godot: OK`. Those runs took 27.87 s and 289.61 s respectively. No compiler code changed in this
plugin-lifetime follow-up. Python client tests: **9 passed**.

From `editors/jetbrains`, using the installed JDK 25:

```sh
./gradlew --offline check buildPlugin verifyPlugin
python3 -m unittest discover -s tests -v
./gradlew --offline runIdeaWithoutLsp4ij -PuiProbe
./gradlew --offline runIdea -PuiProbe
KUMIKAE_COMPLETION_RETENTION_CHECK=1 ./gradlew --offline runIdea -PuiProbe
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

## Incremental checking (November 16)

General incremental checking replaced the body-only fast path: an edit redoes the declarations of the files
it reaches, and bodies are checked a file at a time, the asked-about file first (design in the
[editor guide](../../editors/completion.md#performance-and-ownership) and
[src/README](../../compiler/src/README.md#checking-as-the-program-is-edited)).

### Correctness

`test_incremental` and `test_server_incremental` compare every answer with a fresh whole check after random,
seeded edits; the suite runs a short version (about 800 edits and 300 server edits). Longer runs on
2026-11-16, each sequence of 150 or 200 edits:

| Run | Edits | Result |
|---|---:|---|
| Generated projects, seeds 1000–1599 (release) | 129,900 | 0 differences |
| Server sessions, seeds 1000–1299 (release) | 71,773 | 0 differences |
| as3-signals, flashpunk, flixel, as3corelib (release, Godot loaded) | 1,920 | 0 differences |
| Generated projects, seeds 2000–2119 (ASan/UBSan) | 19,500 | 1 difference, no sanitizer report |
| Generated projects, seeds 3000–3599 (release) | 129,894 | 1 difference |
| Server sessions, seeds 3000–3299; five external projects (release) | 71,933; 1,600 | 0 differences |
| Final code (`9b0655e`): generated, seeds 7000–7299 (release) | 64,950 | 0 differences |
| Final code: server sessions, seeds 7000–7149 (release) | 35,613 | 0 differences |
| Final code: five external projects (release, Godot loaded) | 1,200 | 0 differences |
| Final code: generated and server, seeds 8000– (ASan/UBSan) | 6,500 and 3,523 | 0 differences, no sanitizer report |

The two differences were one bug: the names in a written type's type arguments (`Vector.<Particle>`) were
not recorded as read by the declarations. It is fixed, with a named test, and both seeds pass. Each widening
of the generator found a bug before that (property-half signatures, packages gaining a definition through a
moved declaration, interface obligations from repeated members, imports in local functions); each has a
named test. The longest runs counted 21,277 cascades into readers' declarations, 371 rounds that took in a
cycle, 11,212 symbols of gone declarations used again, 8,486 changes of which file defines a name first and
258,943 doubt judgements. Breaking the dependencies on purpose (no member keys; no readers) makes the quick
version fail at once.

### Speed

`tests/bench/recheck.py` runs `bench_recheck` from one source built in both trees, turn and turn about, on
a project from `generate_project.py --hub` (every file calls `Hub.tick`; each file's `step0` is called by
the file before it). Medians of five runs of ten edit/undo pairs, Godot loaded, ms, at `6bb5bec` against
`5732e88`; load average 8.6 when the sizes started (it had been above 100 for an hour before).

| Edit, until | 100k lines before | after | 1M lines before | after |
|---|---:|---:|---:|---:|
| Body, file checked | 0.275 | 0.307 | 0.570 | 0.600 |
| Body, program checked | 0.276 | 0.308 | 0.571 | 0.614 |
| Method parameters, file | 30.897 | 0.306 | 227.109 | 0.603 |
| Method parameters, program | 30.897 | 0.407 | 227.109 | 0.724 |
| New member, program | 31.118 | 0.311 | 229.521 | 0.606 |
| Hub method, file (a request) | 30.772 | 0.084 | 228.857 | 0.599 |
| Hub method, program | 30.772 | 22.255 | 228.857 | 224.547 |
| First check | 90.1 | 104.7 | 653.3 | 783.0 |
| Peak memory, MiB | 82.8 | 85.1 | 558.4 | 580.8 |

Over the protocol (`completion.py`, 21 samples each, both servers in turn; `program` is the edit followed by
find references, which waits for every file):

| Mode, size: request | before median / p95 | after median / p95 |
|---|---:|---:|
| Signature, 100k: edit + completion | 32.688 / 33.681 | 1.093 / 1.199 |
| Signature, 100k: 20 edits + completion | 37.162 / 38.124 | 6.039 / 6.318 |
| Signature, 100k: edit + program | 35.537 / 36.546 | 5.038 / 5.230 |
| Signature, 1M: edit + completion | 226.712 / 240.350 | 3.764 / 5.251 |
| Signature, 1M: edit + program | 268.062 / 309.268 | 44.510 / 54.290 |
| Hub, 100k: edit + completion in another file | 34.014 / 35.565 | 0.847 / 1.029 |
| Hub, 100k: edit + program | 35.841 / 39.628 | 27.321 / 30.371 |
| Hub, 1M: edit + completion in another file | 223.557 / 238.121 | 4.435 / 4.932 |
| Hub, 1M: edit + program | 259.288 / 262.469 | 264.703 / 268.689 |
| Warm completion, 100k / 1M | 0.123 / 0.172 | 0.113 / 0.151 |
| Recovery, 100k: edit + completion | 32.043 / 32.962 | 1.299 / 1.398 |
| Recovery, 100k: 20 edits + completion | 42.510 / 43.172 | 11.802 / 12.485 |
| Body, 100k: edit + completion | 1.313 / 1.452 | 1.479 / 1.813 |

Load averages 4.4–6.0, except the hub 1M program row (first run, 4.5) and the recovery and body rows (first
run, 4.6–4.7); a second run of those was taken at a load above 200 and is not used. Over the protocol the
1M times include parsing the 5,003-line or generated file's JSON text. A request is answered once the
edit's declarations and the asked file are checked; the rest follows in the background, and stops for any
message. `kas3c check` of 100,000 lines (`run_bench.py --against`, 11 runs): 96.6 ms against 96.0 ms
(+0.6%, within the 4–7 ms spread); `run`, `vectors` and `values` within their spreads.

What costs more: a body edit, by 0.03–0.04 ms (the file's declarations are redone and fingerprinted, and
what it read recorded); the first check, by 16–20% (fingerprints and dependency lists for every file;
bodies are then checked in the background, so the first request waits only for declarations: cold
completion at 1M lines took 535 ms against 640 ms); memory, by 3–4%.

A soak of 4,000 declaration renames (a method and a class renamed to a new name and back, `bench_recheck
--soak`) at 100k lines held resident memory at 91.3 MiB from the 400th edit on.

### After the review (2026-11-16)

A review of the above found that a file added or removed started the declarations over (so did a chain
of more than 64 classes), that a package was kept for every name ever seen, that a declaration file's tag
errors were added again at every body check, and that an override check depended on the order of the files.
Each is fixed with a named test in `test_incremental_named` or `test_names`, and the oracle now adds and
removes files, renames packages, overrides accessor halves along a chain and has a declaration file with
mistaken tags in one sequence in four; a sequence fails if anything but a changed declaration file starts
the declarations over. A file is redone at most eight times in one edit before the check starts over (a
guard; no test or oracle run reaches it): a changed class takes every class below it in the same round, so
a chain of 70 and of 1,000 classes is an edit.

Runs on the final code (`ccb82b9`), each sequence 150 edits (40 for the external projects):

| Run | Edits | Result |
|---|---:|---|
| Generated projects, seeds 11000–11299 (release) | 48,750, 2,519 changes of the files | 0 differences |
| Server sessions, seeds 11000–11149 (release) | 28,480 | 0 differences |
| as3-signals, flashpunk, flixel, as3corelib, starling (release, Godot loaded) | 1,200 | 0 differences |
| Generated and server, seeds 12000– (ASan/UBSan) | 4,900 and 3,088 | 0 differences, no sanitizer report |

Before the last three fixes, at `21000c1`: 86,600 generated edits (4,505 changes of the files), 50,360
server edits, 1,200 on the external projects and 6,500 and 3,814 under ASan/UBSan, no difference.

A file joining and leaving the program (`bench_recheck`, as above; before is the first round, `d39fd67`),
ms, load average 27–29:

| Until the program is checked | 100k lines before | after | 1M lines before | after |
|---|---:|---:|---:|---:|
| A file joins | 108.657 | 0.036 | 880.257 | 0.066 |
| A file leaves | 107.336 | 0.008 | 962.251 | 0.034 |
| Body edit | 0.578 | 0.477 | 1.531 | 0.517 |
| Method parameters | 0.695 | 0.621 | 1.832 | 0.704 |
| Hub method | 51.565 | 50.717 | 541.393 | 507.417 |

The machine was busy (the hub edit at 1M lines took 225 ms on 2026-11-15), so only each pair is
comparable. The `Workspace` now looks only at the files touched since it last checked, which is what the
body edit at 1M lines gains. Against `5732e88` in the same session: a file joins in 0.059 ms against
367.264 ms at 1M lines, and leaves in 0.034 ms against 362.954 ms.

A soak of 20,000 renames at 100k lines (a method, a class and a package, each to a new name and back) held
resident memory at 88.4 MiB from the 10,000th on, from 86.8 MiB at its start.

Reading a declaration file's tags when it is declared, not when a body first uses the class, means its
mistakes are said even when no file uses the class.

## Validation boundaries

Only the stated macOS IDE endpoints were exercised; other IDE builds need a download and were not
verified. The probe shows hover through quick documentation: a mouse hover is not shown while another
application has the desktop's focus, as it does in the background sandbox. Windows transport and filesystem paths have
host-side regressions but still need Windows runtime validation. Human acceptance of the complete
editing workflow remains separate from automated sandbox checks.

An edited `--declarations` file starts the declarations over (bodies are still checked lazily); a file
added to or removed from the program no longer does. An edit to a declaration every file reads
checks every file's bodies again, in the background. The idle check stops between files for any message.
Dynamic receivers need a known type for member completion. The [editor guide](../../editors/completion.md)
describes completion behavior; broader language-server work remains in the [backlog](../backlog.md).
