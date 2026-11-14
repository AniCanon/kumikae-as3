package app.anicanon.kumikae.as3.panel

import java.awt.Color
import java.awt.Font
import java.awt.GradientPaint
import java.awt.image.BufferedImage

/** Synthetic service for the UI probe: a recognisable picture, a long console and scripted failures. */
internal class UiProbeRepository : SimulatorRepository {
    private val known = listOf(
        SimulatorDevice("probe-phone", "Probe Phone 17 Pro", "iOS 26.4", "Shutdown"),
        SimulatorDevice("probe-tablet", "Probe Tablet Air 13-inch", "iOS 26.4", "Shutdown"),
    )

    @Volatile var inputEnabled = false

    @Volatile var installFailure: String? = null

    @Volatile var closed = false

    /** While set, a boot waits on it, so the probe can act during a running action. */
    @Volatile var bootGate: java.util.concurrent.CountDownLatch? = null
    private val log = java.util.concurrent.CopyOnWriteArrayList<String>()
    private var selected: SimulatorDevice? = null

    val calls: List<String> get() = log.toList()

    private fun record(call: String) {
        log += call
    }

    private fun current() = SimulatorStatus(
        selected,
        inputEnabled,
        if (inputEnabled) "idb input is on for this device." else "Viewer mode. Use Device Hub for input.",
    )

    override fun connect(): String {
        record("connect")
        return "/probe/system/kumikae-as3/simulator-0000/session.json"
    }

    override fun devices(): List<SimulatorDevice> {
        record("devices")
        return known.map { if (it.id == selected?.id) checkNotNull(selected) else it }
    }

    override fun status(): SimulatorStatus {
        record("status")
        return current()
    }

    override fun select(device: String): SimulatorStatus {
        record("select $device")
        selected = known.first { it.id == device }
        return current()
    }

    override fun enableInput(): Boolean {
        record("enableInput")
        inputEnabled = true
        return true
    }

    override fun terminate(device: String, bundle: String) = record("terminate $device $bundle")

    override fun shutdown(device: String): SimulatorStatus {
        record("shutdown $device")
        selected = selected?.copy(state = "Shutdown")
        return current()
    }

    override fun boot(device: String): SimulatorStatus {
        record("boot $device")
        bootGate?.await()
        selected = selected?.copy(state = "Booted")
        return current()
    }

    override fun install(device: String, appPath: String) {
        record("install $device $appPath")
        installFailure?.let { error(it) }
    }

    override fun launch(device: String, bundle: String) = record("launch $device $bundle")

    override fun screenshot(device: String): SimulatorFrame {
        record("screenshot $device")
        return SimulatorFrame(device, 1, picture(), captureMs = 435)
    }

    override fun consolePage(cursor: Long): ConsolePage {
        record("console $cursor")
        if (cursor > 0) return ConsolePage(emptyList(), cursor, false)
        val lines = (1..LINES).map { ConsoleEntry("stdout", "test $it of $LINES passed: a line long enough to wrap\n") }
        return ConsolePage(listOf(ConsoleEntry("session", "Launching org.example.probe\n")) + lines, LINES + 1L, false)
    }

    override fun input(device: String, input: SimulatorInput) = record("input $device $input")

    override fun close() {
        closed = true
    }

    /** A phone-shaped picture with a frame on its edge, so a cut-off or stretched picture is easy to see. */
    private fun picture(): BufferedImage {
        val image = BufferedImage(WIDTH, HEIGHT, BufferedImage.TYPE_INT_RGB)
        val canvas = image.createGraphics()
        canvas.paint = GradientPaint(0f, 0f, Color(0x2D6CDF), 0f, HEIGHT.toFloat(), Color(0x7A2DDF))
        canvas.fillRect(0, 0, WIDTH, HEIGHT)
        canvas.color = Color(0xFFD23F)
        canvas.fillRect(0, 0, WIDTH, EDGE)
        canvas.fillRect(0, HEIGHT - EDGE, WIDTH, EDGE)
        canvas.fillRect(0, 0, EDGE, HEIGHT)
        canvas.fillRect(WIDTH - EDGE, 0, EDGE, HEIGHT)
        canvas.color = Color.WHITE
        canvas.font = Font(Font.SANS_SERIF, Font.BOLD, WIDTH / 12)
        canvas.drawString("Probe screen", WIDTH / 8, HEIGHT / 2)
        canvas.fillOval(WIDTH / 2 - WIDTH / 6, HEIGHT / 5, WIDTH / 3, WIDTH / 3)
        canvas.dispose()
        return image
    }

    private companion object {
        const val WIDTH = 603
        const val HEIGHT = 1311
        const val EDGE = 24
        const val LINES = 40
    }
}
