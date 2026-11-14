package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel
import app.anicanon.kumikae.as3.simulator.ProcessRunner
import java.lang.management.ManagementFactory
import java.nio.file.Files
import java.nio.file.Path

/** Explicit live-probe extension; reports retained heap and CPU without retaining frame samples. */
internal object UiProbeEndurance {
    fun run(
        repository: SimulatorRepository,
        device: String,
        frame: () -> SimulatorFrame,
        drain: () -> List<Pair<Long, Double>>,
        edt: Long,
        destination: Path,
    ) {
        val process = ProcessHandle.current()
        val memory = ManagementFactory.getMemoryMXBean()
        val threads = ManagementFactory.getThreadMXBean()
        val start = System.nanoTime()
        var previous = start
        var nextSample = start
        var index = 0
        val rows = mutableListOf(
            "seconds,window_fps,observed_p95_ms,heap_mib,ide_rss_mib,helper_rss_mib,ide_cpu_s,edt_cpu_s,helper_cpu_s",
        )
        fun cpu(child: ProcessHandle): Double = ProcessRunner().run(
            listOf("/bin/ps", "-p", child.pid().toString(), "-o", "time="),
            1000,
        ).trim().split(':').fold(0.0) { total, part -> total * 60 + part.toDouble() }
        fun rss(child: ProcessHandle): Double = ProcessRunner().run(
            listOf("/bin/ps", "-p", child.pid().toString(), "-o", "rss="),
            1000,
        ).trim().toDouble() / 1024
        fun sample() {
            val now = System.nanoTime()
            val pictures = drain()
            val elapsed = (now - previous) / 1e9
            val delays = pictures.map { it.second }.sorted()
            val fps = if (previous == start) 0.0 else pictures.size / elapsed
            val p95 = delays.takeIf { it.isNotEmpty() }?.get(((delays.size - 1) * .95).toInt()) ?: 0.0
            val helper = process.descendants().use { children ->
                children.filter { it.info().command().orElse("").endsWith("/simulator-bridge") }
                    .findFirst().orElseThrow()
            }
            System.gc()
            rows += listOf(
                (now - start) / 1e9, fps, p95, memory.heapMemoryUsage.used / 1048576.0,
                rss(process), rss(helper), cpu(process), threads.getThreadCpuTime(edt) / 1e9, cpu(helper),
            ).joinToString(",")
            Files.write(destination.resolve("live-endurance.csv"), rows)
            previous = now
        }
        drain()
        do {
            if (System.nanoTime() >= nextSample) {
                sample()
                nextSample += 60_000_000_000L
            }
            val shown = frame()
            val size = shown.geometry
            val up = index++ % 2 == 0
            repository.input(
                device,
                SimulatorInput.Swipe(
                    shown.generation,
                    size,
                    Pixel(size.width / 2, (size.height * if (up) .8 else .3).toInt()),
                    Pixel(size.width / 2, (size.height * if (up) .3 else .8).toInt()),
                    700,
                ),
            )
            Thread.sleep(100)
        } while (System.nanoTime() - start < 600_000_000_000L)
        sample()
    }
}
