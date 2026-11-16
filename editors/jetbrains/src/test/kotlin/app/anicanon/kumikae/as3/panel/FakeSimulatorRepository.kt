package app.anicanon.kumikae.as3.panel

import java.awt.image.BufferedImage

/** Scripted repository: tests set the replies, read the recorded calls and may make the next call fail. */
internal class FakeSimulatorRepository : SimulatorRepository {
    val phone = SimulatorDevice("A", "Phone", "iOS 26.4", "Booted")
    val tablet = SimulatorDevice("B", "Tablet", "iOS 26.4", "Shutdown")
    var listed = listOf(phone, tablet)
    val calls = mutableListOf<String>()
    var selected: SimulatorDevice? = null
    var inputEnabled = false
    var notice = "Viewer mode."
    var page = ConsolePage(emptyList(), 0, false)
    var failure: String? = null
    var closed = false
    var nativeInstalled = true
    val feeds = mutableListOf<FakeFrames>()

    class FakeFrames : SimulatorFrames {
        var closed = false
        override val ended: Boolean get() = closed
        override var failure: Throwable? = null
        var next: SimulatorFrame? = null
        override fun poll(): SimulatorFrame? = next.also { next = null }
        override fun close() {
            closed = true
        }
    }

    override fun openFrames(device: String): SimulatorFrames {
        record("stream $device")
        return FakeFrames().also { feeds += it }
    }

    private fun record(call: String) {
        calls += call
        failure?.let {
            failure = null
            error(it)
        }
    }

    override fun connect(): String {
        record("connect")
        return "/state/session.json"
    }

    override fun devices(): List<SimulatorDevice> {
        record("devices")
        return listed
    }

    override fun status(): SimulatorStatus {
        record("status")
        return SimulatorStatus(selected, inputEnabled, notice)
    }

    override fun select(device: String): SimulatorStatus {
        record("select $device")
        selected = listed.first { it.id == device }
        return SimulatorStatus(selected, inputEnabled, notice)
    }

    override fun boot(device: String): SimulatorStatus {
        record("boot $device")
        selected = selected?.copy(state = "Booted")
        return SimulatorStatus(selected, inputEnabled, notice)
    }

    override fun enableInput(): Boolean {
        record("enableInput")
        inputEnabled = nativeInstalled
        return nativeInstalled
    }

    override fun shutdown(device: String): SimulatorStatus {
        record("shutdown $device")
        selected = selected?.copy(state = "Shutdown")
        return SimulatorStatus(selected, inputEnabled, notice)
    }

    override fun terminate(device: String, bundle: String) = record("terminate $device $bundle")

    override fun install(device: String, appPath: String) = record("install $device $appPath")

    override fun launch(device: String, bundle: String, sources: String) =
        record("launch $device $bundle" + if (sources.isEmpty()) "" else " from $sources")

    override fun screenshot(device: String): SimulatorFrame {
        record("screenshot $device")
        return SimulatorFrame(device, 1, BufferedImage(4, 8, BufferedImage.TYPE_INT_RGB), 435)
    }

    override fun consolePage(cursor: Long): ConsolePage {
        record("console $cursor")
        return page
    }

    override fun input(device: String, input: SimulatorInput) = record("input $device $input")

    override fun close() {
        closed = true
    }
}
