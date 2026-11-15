package app.anicanon.kumikae.as3.simulator

import com.google.gson.GsonBuilder
import com.google.gson.JsonParser
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicBoolean

class NativeBridgeConnection private constructor(
    private val child: StreamProcess,
    private val replies: ProcessLines,
    val slot: Path,
    val geometry: Geometry,
    val density: Double,
) : AutoCloseable {
    private val closed = AtomicBoolean()
    private var serial = 0L
    private val gson = GsonBuilder().disableHtmlEscaping().create()
    val alive: Boolean get() = !closed.get() && child.alive
    val errors: String get() = child.errors
    var lastCompletedNanos: Long? = null
        private set
    var clockOffsetNanos = 0L
        private set
    var clockUncertaintyNanos = 0L
        private set

    @Synchronized fun send(
        operation: String,
        x: Double? = null,
        y: Double? = null,
        text: String? = null,
        orientation: String? = null,
    ): Long {
        check(!closed.get()) { "Native Simulator bridge is closed" }
        val id = ++serial
        val command = mutableMapOf<String, Any>("id" to id, "op" to operation)
        x?.let { command["x"] = it }
        y?.let { command["y"] = it }
        text?.let { command["text"] = it }
        orientation?.let { command["orientation"] = it }
        try {
            child.input.write((gson.toJson(command) + "\n").toByteArray(Charsets.UTF_8))
            child.input.flush()
            val reply = JsonParser.parseString(replies.next()).asJsonObject
            check(reply["id"]?.asLong == id) { reply["error"]?.asString ?: "Native input reply was out of order" }
            check(reply["ok"]?.asBoolean == true) { reply["error"]?.asString ?: "Native input failed" }
            val received = reply["receivedNanos"].asLong
            lastCompletedNanos = reply["completedNanos"]?.asLong?.also {
                require(it >= received) { "Native completion precedes receipt" }
            }
            return received
        } catch (failure: Exception) {
            close()
            if (failure is InterruptedException) throw failure
            throw SimulatorException("INPUT_UNAVAILABLE", "${failure.message}; input is not replayed")
        }
    }

    override fun close() {
        if (closed.compareAndSet(false, true)) replies.close()
    }

    fun awaitClosed() {
        close()
        child.awaitClosed()
    }

    private fun calibrate() {
        val before = System.nanoTime()
        val helper = send("ping")
        val after = System.nanoTime()
        clockOffsetNanos = before + (after - before) / 2 - helper
        clockUncertaintyNanos = (after - before) / 2
    }

    companion object {
        fun open(
            executable: String,
            device: String,
            directory: Path,
            processes: StreamProcesses,
        ): NativeBridgeConnection {
            val slot = directory.resolve("frame.slot")
            val child = processes.start(listOf(executable, "--device", device, "--frames", slot.toString()))
            val replies = ProcessLines(child, "native-bridge", 10_000)
            try {
                val ready = JsonParser.parseString(replies.next()).asJsonObject
                if (ready["code"]?.asString == "DEVICE_UNAVAILABLE") {
                    throw SimulatorException("DEVICE_UNAVAILABLE", ready["error"].asString)
                }
                check(ready["ready"]?.asBoolean == true) {
                    ready["error"]?.asString
                        ?: "Native bridge did not become ready"
                }
                val width = ready["width"].asLong
                val height = ready["height"].asLong
                val density = ready["density"].asDouble
                require(
                    width in 1..NativeFrameLayout.maxDimension && height in 1..NativeFrameLayout.maxDimension &&
                        density.isFinite() && density in 1.0..10.0,
                ) { "Invalid native Simulator geometry" }
                return NativeBridgeConnection(child, replies, slot, Geometry(width.toInt(), height.toInt()), density)
                    .also { it.calibrate() }
            } catch (failure: Exception) {
                replies.close()
                child.awaitClosed()
                throw failure
            }
        }
    }
}
