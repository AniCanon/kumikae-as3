package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.simulator.Pixel
import app.anicanon.kumikae.as3.simulator.ProcessRunner
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.util.Disposer
import com.intellij.util.ui.JBUI
import java.awt.event.MouseEvent
import java.lang.management.ManagementFactory
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicLong
import javax.swing.JFrame

/** Explicit opt-in measurement only: uses the already booted demo device, Home and Settings. */
internal object UiProbeLive {
    fun run(destination: Path) {
        val ownedDevice =
            requireNotNull(System.getenv("KUMIKAE_SIMULATOR_PROBE_DEVICE")) { "Supply the probe-owned booted device." }
        val directory = Files.createTempDirectory("kumikae-live-probe")
        val service =
            LocalSimulatorService(
                directory,
                nativeExecutable = PluginFiles.native()?.resolve(
                    "simulator-bridge",
                ),
                closeLater = {
                    it()
                },
            )
        val delegate = ApiSimulatorRepository(service)
        val release = AtomicLong()
        val completedTouches = AtomicLong()
        val taps = CopyOnWriteArrayList<Double>()
        val paints = CopyOnWriteArrayList<Pair<Long, Double>>()
        val repository = object : SimulatorRepository by delegate {
            override fun touch(on: SimulatorFrame, phase: String, at: Pixel) {
                delegate.touch(on, phase, at)
                if (phase ==
                    "up"
                ) {
                    completedTouches.incrementAndGet()
                    release.getAndSet(0).takeIf { it > 0 }?.let { taps += (System.nanoTime() - it) / 1e6 }
                }
            }
            override fun input(device: String, input: SimulatorInput) {
                delegate.input(device, input)
                release.getAndSet(0).takeIf { it > 0 }?.let { taps += (System.nanoTime() - it) / 1e6 }
            }
        }
        lateinit var controller: SimulatorWindowController
        lateinit var window: SimulatorToolWindow
        lateinit var host: JFrame
        onEdt {
            controller = SimulatorComposition.controller(repository, { true }, {})
            window = SimulatorToolWindow(controller, UiProbePrompts())
            window.screen.onFramePresented = { frame, now ->
                if (frame.receivedNanos > 0) paints += now to (now - frame.receivedNanos) / 1e6
            }
            host = JFrame("Simulator performance probe").apply {
                contentPane.add(window.component)
                setSize(420, 900)
                isVisible = true
            }
        }
        try {
            await { controller.devices.any { it.id == ownedDevice && it.state == BOOTED } }
            onEdt {
                window.state.listShown = false
                controller.open(controller.devices.single { it.id == ownedDevice && it.state == BOOTED })
            }
            await { window.screen.frame != null && controller.inputEnabled }
            check(onEdtValue { controller.continuousTouch }) {
                onEdtValue { "Native route unavailable: ${controller.notice}; ${controller.status}" }
            }
            val id = onEdtValue { checkNotNull(controller.selected).id }
            repository.input(id, SimulatorInput.Press(HardwareButton.HOME))
            Thread.sleep(1500)
            repeat(12) {
                onEdt {
                    val screen = window.screen
                    val frame = checkNotNull(screen.frame)
                    val place = DeviceFrame(
                        frame.image.width,
                        frame.image.height,
                        screen.width,
                        screen.height,
                        JBUI.scale(18),
                        island = true,
                    ).screen
                    val x = (place.x + place.width * 0.5).toInt()
                    val y = (place.y + place.height * 780.0 / 874).toInt()
                    release.set(System.nanoTime())
                    screen.dispatchEvent(
                        MouseEvent(
                            screen, MouseEvent.MOUSE_PRESSED, System.currentTimeMillis(),
                            0, x, y, 1, false, MouseEvent.BUTTON1,
                        ),
                    )
                    screen.dispatchEvent(
                        MouseEvent(
                            screen, MouseEvent.MOUSE_RELEASED, System.currentTimeMillis(),
                            0, x, y, 1, false, MouseEvent.BUTTON1,
                        ),
                    )
                }
                await { taps.size > it }
                Thread.sleep(100)
            }
            val launchStart = System.nanoTime()
            ProcessRunner().run(listOf("/usr/bin/xcrun", "simctl", "launch", id, "com.apple.Preferences"), 15_000)
            await { paints.any { it.first > launchStart } }
            val launchDelay = (paints.first { it.first > launchStart }.first - launchStart) / 1e6
            Thread.sleep(1000)
            val previousTouches = completedTouches.get()
            val dragPaints = UiProbePointer.drag(window.screen) { paints.size }
            await { completedTouches.get() > previousTouches }
            Thread.sleep(500)
            paints.clear()
            val start = System.nanoTime()
            val threads = ManagementFactory.getThreadMXBean()
            val edt = onEdtValue { Thread.currentThread().threadId() }
            val cpuStart = threads.getThreadCpuTime(edt)
            val reader = UiProbeReaderAllocation("kumikae-native-frames")
            repeat(16) { index ->
                val frame = onEdtValue { checkNotNull(controller.frame) }
                val size = frame.geometry
                val up = index % 2 == 0
                repository.input(
                    id,
                    SimulatorInput.Swipe(
                        frame.generation,
                        size,
                        Pixel(
                            size.width / 2,
                            (size.height * if (up) .8 else .3).toInt(),
                        ),
                        Pixel(
                            size.width / 2,
                            (size.height * if (up) .3 else .8).toInt(),
                        ),
                        700,
                    ),
                )
                Thread.sleep(100)
            }
            val seconds = (System.nanoTime() - start) / 1e9
            val allocated = reader.bytes()
            val edtCpuSeconds = (threads.getThreadCpuTime(edt) - cpuStart) / 1e9
            val resolution = onEdtValue { checkNotNull(controller.frame).geometry }
            val fps = paints.size / seconds
            val delays = paints.map { it.second }.sorted()
            if (System.getenv("KUMIKAE_SIMULATOR_ENDURANCE_PROBE") == "1") {
                UiProbeEndurance.run(repository, id, {
                    onEdtValue { checkNotNull(controller.frame) }
                }, {
                    onEdtValue { paints.toList().also { paints.clear() } }
                }, edt, destination)
            }
            repository.input(id, SimulatorInput.Press(HardwareButton.HOME))
            Thread.sleep(2500)
            paints.clear()
            Thread.sleep(5000)
            val idlePaints = paints.size
            onEdt { host.isVisible = false }
            val count = paints.size
            Thread.sleep(1500)
            val hiddenPaints = paints.size - count
            val hiddenHelpers = ProcessHandle.current().descendants().use { children ->
                children.filter { it.info().command().orElse("").endsWith("/simulator-bridge") }.count()
            }
            Files.writeString(
                destination.resolve("live-performance.txt"),
                "painted_fps=$fps\nobserved_to_paint_p50_ms=${percentile(delays, .5)}\n" +
                    "observed_to_paint_p95_ms=${percentile(delays, .95)}\n" +
                    "mouse_press_to_up_ack_ms=${taps.joinToString()}\n" +
                    "reader_allocated_bytes=$allocated\n" +
                    "resolution=$resolution\nedt_cpu_seconds=$edtCpuSeconds\nmeasurement_seconds=$seconds\n" +
                    "drag_frames_before_release=$dragPaints\n" +
                    "launch_request_to_next_paint_ms=$launchDelay\n" +
                    "idle_new_paints_in_5s=$idlePaints\nhidden_new_paints_in_1.5s=$hiddenPaints\n" +
                    "native_helpers_while_hidden=$hiddenHelpers\n",
            )
            check(fps > 0) { "Native stream painted no changing frames" }
            check(hiddenPaints == 0) { "Hidden panel still paints" }
            check(hiddenHelpers == 0L) { "Hidden panel still owns $hiddenHelpers native helpers" }
        } finally {
            onEdt {
                Disposer.dispose(controller)
                host.dispose()
            }
            directory.toFile().deleteRecursively()
        }
    }

    private fun percentile(values: List<Double>, fraction: Double) = values[(values.lastIndex * fraction).toInt()]
    private fun onEdt(work: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(work, ModalityState.any())
    private fun <T> onEdtValue(work: () -> T): T {
        var value: T? = null
        onEdt { value = work() }
        return checkNotNull(value)
    }
    private fun await(condition: () -> Boolean) {
        val deadline = System.nanoTime() + 15_000_000_000L
        while (System.nanoTime() < deadline) {
            if (onEdtValue(condition)) return
            Thread.sleep(20)
        }
        error("Live probe condition timed out")
    }
}
