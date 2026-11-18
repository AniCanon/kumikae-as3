# Embedded device performance in Rider

These measurements compare Kumikae AS3 0.2.3 and 0.2.4 in a Rider 2026.2.3.1
sandbox on one Apple M5 Pro. Each run used one warmed-up virtual device. Frame rates
count new pictures painted in Rider. The latency samples end at the panel's paint
callback. They exclude the host compositor and monitor.

| Device and workload | Plugin | Painted fps | Median latency | 95th percentile |
|---|---|---:|---:|---:|
| iOS Settings, repeated scrolling | 0.2.3 | 69.2 | 17.7 ms | 23.3 ms |
| iOS Settings, repeated scrolling | 0.2.4 | 74.7 | 10.7 ms | 13.2 ms |
| Android game, host GPU | 0.2.3 | 59.9 | 4.0 ms | 4.5 ms |
| Android game, host GPU | 0.2.4 | 59.9 | 3.4 ms | 3.8 ms |
| Android game, software renderer | 0.2.4 | 23.4 | 3.6 ms | 4.1 ms |

## iOS Simulator

The probe scrolls Settings sixteen times, alternating direction, with a 700 ms
gesture. It times pictures from native observation through painting the 1206 × 2622
source in Rider. The old reader allocated roughly 12 MiB of pixel storage per new
picture, and an 8 ms UI timer checked for the next frame. The new reader reuses a
buffer after the panel releases it and signals the UI as soon as a picture arrives.
While the UI is busy, one pending notification presents the latest picture.

During 13 seconds of scrolling, UI-thread CPU time fell from 5.57 to 4.19 seconds.
The new reader thread allocated 2.15 MB **in total** over that interval. A repeat
of the new build measured 75.0 fps and 13.3 ms at the 95th percentile. The idle
panel painted zero new pictures in five seconds. Hiding it stopped the native helper.

An earlier run measured 26.2 fps. The isolated baseline did not reproduce that
slowdown. These measurements establish the cost of the old display path during
scrolling. They do not establish what happened during that earlier session or
measure game rendering and input-to-display latency.

## Android Emulator

Rider builds and launches SupportSprint, taps Start, verifies the roster appears
and samples five seconds of animation. The latency columns start after the reader
has copied the pixels. To identify upstream bottlenecks, the probe also uses the
native sequence numbers and their observation timestamps.

The earlier 19 fps run used SwiftShader. Switching only the renderer on the same
AVD with Emulator 37.2.13 supplied about 60 frames/s on the host GPU versus 28.7
with software graphics on the new build. Rider painted 315 and 123 frames in the
two sample windows, respectively. The host-GPU reader allocated 0.74 MB during
the sample. Changing the GPU improved the source rate. Buffer reuse improved
the time to paint on a host GPU that already sustained 60 fps.

Android Studio requests a stream sized to the viewport and reuses a detached
image while presenting the latest frame. Kumikae uses the same buffer ownership
principle with its existing capture transport. These numbers do not compare
the two IDEs head to head. [Android Studio emulator view source](https://android.googlesource.com/platform/tools/adt/idea/+/3cbd9787632f7320eda0a14134552ff55c78b357/streaming/src/com/android/tools/idea/streaming/emulator/EmulatorView.kt)

## Repeating the measurements

In the plugin directory, run `./gradlew runIde -PuiProbe` with the environment
for the desired probe. Use a disposable device and keep the other emulator stopped.

- For iOS, set `KUMIKAE_DEVICE_PERF_PROBE=1` and `KUMIKAE_SIMULATOR_PROBE_DEVICE`
  to the booted Simulator's identifier. Read `build/reports/ui/live-performance.txt`.
- For Android, set `KUMIKAE_GAME_RUN_PROBE` to the fixture directory containing
  the SupportSprint project as `android-game`. Set `KUMIKAE_ANDROID_PROBE_SERIAL`
  to the running emulator's serial. Read `build/reports/ui/android-display.txt`.
  Repeat with the same AVD and the other graphics setting.
