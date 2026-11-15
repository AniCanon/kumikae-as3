package app.anicanon.kumikae.as3.simulator

import org.junit.Assume.assumeTrue
import org.junit.Test
import java.lang.management.ManagementFactory
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlin.test.assertTrue

/** Controlled source, production helper and reader, no IDE/window/host input. */
class NativePerformanceProbe {
    private data class Clock(val offset: Long, val uncertainty: Long)
    private fun calibrate(bridge: NativeBridgeConnection): Clock = (1..15).map {
        val before = System.nanoTime()
        val native = bridge.send("ping")
        val after = System.nanoTime()
        Clock(before + (after - before) / 2 - native, (after - before) / 2)
    }.minBy { it.uncertainty }

    private data class Sample(val at: Long, val sequence: Long, val tick: Int, val copy: Long, val age: Long)

    @Test fun controlledAnimationAndInput() {
        assumeTrue(System.getenv("KUMIKAE_NATIVE_PERFORMANCE_PROBE") == "1")
        val seconds = System.getenv("KUMIKAE_NATIVE_PROBE_SECONDS")?.toLong() ?: 600L
        require(seconds in 30..600)
        val label = System.getenv("KUMIKAE_NATIVE_PROBE_LABEL") ?: "run"
        require(label.matches(Regex("[a-z0-9-]+")))
        val destination = Path.of("build/reports/native-performance", label).toAbsolutePath()
        Files.createDirectories(destination)
        val diagnostics = destination.resolve("helper.jsonl")
        check(!Files.exists(diagnostics)) { "Choose a fresh probe label" }
        NativeProbeApp(ProcessRunner()).use { id, report ->
            val directory = Files.createTempDirectory("native-performance")
            val processes = OwnedStreamProcesses(4000)
            val instrumented = StreamProcesses { command ->
                processes.start(listOf("/usr/bin/env", "KUMIKAE_NATIVE_METRICS=$diagnostics") + command)
            }
            val helper = Path.of("../native/SimulatorBridge/.build/release/simulator-bridge").toAbsolutePath()
            try {
                NativeBridgeConnection.open(helper.toString(), id, directory, instrumented).use { bridge ->
                    measure(bridge, report, destination, seconds)
                    bridge.awaitClosed()
                }
                println("performance_helper_cleanup=true")
            } finally {
                processes.close()
                Files.deleteIfExists(directory.resolve("frame.slot"))
                Files.delete(directory)
            }
        }
    }

    private fun measure(bridge: NativeBridgeConnection, report: Path, destination: Path, seconds: Long) {
        val clock = calibrate(bridge)
        val samples = ConcurrentLinkedQueue<Sample>()
        val stopped = AtomicBoolean()
        val failure = AtomicReference<Throwable?>()
        val reader = Thread({
            try {
                NativeFrameReader(bridge.slot).use { mapped ->
                    var saved = false
                    while (!stopped.get()) {
                        val before = System.nanoTime()
                        mapped.poll()?.let { frame ->
                            val now = System.nanoTime()
                            var tick = 0
                            val density = frame.header.densityMilli / 1000.0
                            for (bit in 0..<16) {
                                val pixel = frame.image.getRGB(
                                    ((15 + bit * 10) * density).toInt(),
                                    (435 * density).toInt(),
                                )
                                val inverse = frame.image.getRGB(
                                    ((15 + bit * 10) * density).toInt(),
                                    (445 * density).toInt(),
                                )
                                check(((pixel and 255) > 128) != ((inverse and 255) > 128)) {
                                    "Raster tick marker is obscured"
                                }
                                if ((pixel and 255) > 128) tick = tick or (1 shl bit)
                            }
                            samples.add(
                                Sample(
                                    now,
                                    frame.header.sequence,
                                    tick,
                                    now - before,
                                    now - frame.header.observedNanos - clock.offset,
                                ),
                            )
                            if (!saved && samples.size >= 20) {
                                javax.imageio.ImageIO.write(
                                    frame.image,
                                    "png",
                                    destination.resolve("warmup.png").toFile(),
                                )
                                saved = true
                            }
                        }
                        Thread.sleep(4)
                    }
                }
            } catch (error: Exception) {
                if (!stopped.get()) failure.set(error)
            }
        }, "native-probe-reader")
        reader.start()
        try {
            NativeProbeApp.await("changing frame") { samples.size > 20 || failure.get() != null }
            failure.get()?.let { throw it }
            runInputAndWindows(bridge, report, destination, seconds, samples, failure, clock)
            val end = calibrate(bridge)
            Files.writeString(
                destination.resolve("clock-end.txt"),
                "offset_change_ms=${(end.offset - clock.offset) / 1e6}\ncombined_uncertainty_ms=${(end.uncertainty + clock.uncertainty) / 1e6}\n",
            )
        } finally {
            stopped.set(true)
            reader.interrupt()
            reader.join(5000)
            check(!reader.isAlive) { "Probe reader did not stop" }
        }
    }

    private fun runInputAndWindows(
        bridge: NativeBridgeConnection,
        report: Path,
        destination: Path,
        seconds: Long,
        samples: ConcurrentLinkedQueue<Sample>,
        failure: AtomicReference<Throwable?>,
        clock: Clock,
    ) {
        Files.newBufferedWriter(destination.resolve("input.csv")).use { inputWriter ->
            Files.newBufferedWriter(destination.resolve("frame-timeline.csv")).use { frameWriter ->
                frameWriter.appendLine("seconds,sequence,tick,copy_ms,age_ms")
                inputWriter.appendLine("seconds,roundtrip_ms,helper_completion_interval_ms,receipt_estimate_ms")
                var inputEvents = 0
                val frameRows =
                    mutableListOf(
                        "seconds,frames,fps,source_tick_fps,unique_ticks,last_sequence,copy_p95_ms,copy_max_ms,age_p95_ms,gc_count,gc_ms,guest_fps,guest_draw_fps,input_sleep_max_ms",
                    )
                val gc = ManagementFactory.getGarbageCollectorMXBeans()
                var guest = NativeProbeApp.read(report)
                var lastGC = gc.sumOf { it.collectionCount }
                var lastGCMs = gc.sumOf { it.collectionTime }
                val start = System.nanoTime()
                var previous = start
                var nextWindow = start + 10_000_000_000L
                var nextInput = start
                var index = 0
                var touching = false
                var sleepMax = 0L
                samples.clear()
                fun sampleWindow(after: Long) {
                    val window = mutableListOf<Sample>()
                    while ((samples.peek()?.at ?: Long.MAX_VALUE) <= after) window += samples.remove()
                    if (window.isEmpty()) return
                    for (sample in window) {
                        frameWriter.appendLine(
                            listOf(
                                (sample.at - start) / 1e9,
                                sample.sequence,
                                sample.tick,
                                sample.copy / 1e6,
                                sample.age / 1e6,
                            ).joinToString(","),
                        )
                    }
                    frameWriter.flush()
                    val elapsed = (after - previous) / 1e9
                    val nowGuest = NativeProbeApp.read(report)
                    val guestSeconds = nowGuest["uptime"].asDouble - guest["uptime"].asDouble
                    check(guestSeconds > 0 || elapsed < 2) { "Guest report stopped advancing" }
                    fun guestRate(key: String): Double? = if (guestSeconds > 0) {
                        (nowGuest[key].asInt - guest[key].asInt) / guestSeconds
                    } else {
                        null
                    }
                    val tickFps = if (window.size < 2) {
                        null
                    } else {
                        ((window.last().tick - window.first().tick) and 65535) /
                            ((window.last().at - window.first().at) / 1e9)
                    }
                    val copies = window.map { it.copy / 1e6 }.sorted()
                    val ages = window.map { it.age / 1e6 }.sorted()
                    val count = gc.sumOf { it.collectionCount }
                    val time = gc.sumOf { it.collectionTime }
                    frameRows += listOf(
                        (after - start) / 1e9, window.size, window.size / elapsed, tickFps,
                        window.map { it.tick }.distinct().size, window.last().sequence,
                        percentile(copies, .95), copies.last(), percentile(ages, .95), count - lastGC, time - lastGCMs,
                        guestRate("ticks"), guestRate("draws"), sleepMax / 1e6,
                    ).joinToString(",")
                    Files.write(destination.resolve("frames.csv"), frameRows)
                    inputWriter.flush()
                    println(
                        "performance_seconds=${(after - start) / 1e9}; " +
                            "reader_fps=${window.size / elapsed}; guest_fps=${guestRate("ticks")}",
                    )
                    previous = after
                    guest = nowGuest
                    lastGC = count
                    lastGCMs = time
                    sleepMax = 0
                    nextWindow += 10_000_000_000L
                }
                try {
                    while (System.nanoTime() - start < seconds * 1_000_000_000L) {
                        failure.get()?.let { throw it }
                        check(bridge.alive) { "Helper ended: ${bridge.errors}" }
                        val phase = index++ % 42
                        val op = when (phase) {
                            0 -> "down"
                            41 -> "up"
                            else -> "move"
                        }
                        val before = System.nanoTime()
                        val received = bridge.send(op, 50.0 + phase * 2, 350.0)
                        val after = System.nanoTime()
                        touching = op != "up"
                        val completed = checkNotNull(bridge.lastCompletedNanos)
                        inputEvents++
                        inputWriter.appendLine(
                            listOf(
                                (after - start) / 1e9,
                                (after - before) / 1e6,
                                (completed - received) / 1e6,
                                (received + clock.offset - before) / 1e6,
                            ).joinToString(","),
                        )
                        if (after >= nextWindow) sampleWindow(after)
                        nextInput = maxOf(nextInput + 20_000_000L, after)
                        val remaining = nextInput - System.nanoTime()
                        if (remaining > 0) java.util.concurrent.locks.LockSupport.parkNanos(remaining)
                        sleepMax = maxOf(sleepMax, System.nanoTime() - nextInput)
                    }
                    sampleWindow(System.nanoTime())
                } finally {
                    if (touching && bridge.alive) bridge.send("up", 100.0, 350.0)
                }
                Files.writeString(
                    destination.resolve("summary.txt"),
                    "seconds=${(System.nanoTime() - start) / 1e9}\n" +
                        "input_events=$inputEvents\nstart_nanos=$start\nclock_offset_nanos=${clock.offset}\nclock_uncertainty_ms=${clock.uncertainty / 1e6}\n",
                )
                assertTrue(inputEvents > seconds * 10)
            }
        }
    }
    private fun percentile(values: List<Double>, fraction: Double) = values[(values.lastIndex * fraction).toInt()]
}
