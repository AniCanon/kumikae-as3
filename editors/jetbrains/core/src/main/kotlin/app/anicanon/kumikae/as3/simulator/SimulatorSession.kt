package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import java.nio.file.Files
import java.nio.file.Path
import java.util.Base64
import java.util.concurrent.atomic.AtomicBoolean

data class Frame(
    val device: String,
    val width: Int,
    val height: Int,
    val generation: Long,
    val format: String,
    val data: String,
    val capturedAt: Long,
    val captureMs: Long,
)

class SimulatorSession(
    private val directory: Path,
    private val runner: CommandRunner,
    input: InputBackend = NoInput,
    private val launchLog: LaunchLog = LaunchLog(),
) : AutoCloseable {
    @Volatile private var input: InputBackend = input
    private var selected: Device? = null
    private val geometry = FrameGeometry()
    private val snapshots = SnapshotCapture(directory, runner)
    private var streamSerial = 0L
    private var streaming: FrameFeed? = null

    @Volatile private var closed = false

    // Held by the one boot, install or shutdown that is running. They take up to two minutes, so they run
    // outside the session's lock: status, the list, the log and pictures still answer meanwhile.
    private val slow = AtomicBoolean()
    private var sendingInput = false
    private var openingFrames = false

    private var touching = false

    /** Panel-only phases keep HTTP version 1 compatible; every point still quotes its picture. */
    fun touch(device: String, stamp: Long, displayed: Geometry, phase: String, at: Pixel) {
        val (backend, point) = synchronized(this) {
            checkDevice(device)
            if (sendingInput || openingFrames ||
                slow.get()
            ) {
                throw SimulatorException("BUSY", "Simulator input is still running")
            }
            if (streaming?.ended != false || !input.continuousTouch) {
                throw SimulatorException("INPUT_UNAVAILABLE", "Reconnect the native picture before touching it")
            }
            require(phase in setOf("down", "move", "up")) { "Invalid touch phase" }
            if ((phase == "down") == touching) throw SimulatorException("STALE_FRAME", "Touch is no longer active")
            val point = geometry.point(stamp, displayed, at.x.toDouble(), at.y.toDouble(), input.statusScale(device))
            touching = phase != "up"
            sendingInput = true
            input to point
        }
        try {
            backend.send(device, phase, listOf(point.x.toString(), point.y.toString()))
        } catch (failure: Exception) {
            synchronized(this) {
                touching = false
                invalidate()
            }
            throw failure
        } finally {
            synchronized(this) { sendingInput = false }
        }
    }

    fun dispatch(method: String, params: JsonObject): Any {
        if (closed) throw SimulatorException("CLOSED", "Session is closed")
        return when (method) {
            "devices" -> devices()

            "select" -> select(params["device"].asString)

            "screenshot" -> {
                val frame = screenshot(params["device"].asString, params["format"]?.asString ?: "png")
                val picture = frame.picture
                Frame(
                    frame.device,
                    picture.size.width,
                    picture.size.height,
                    frame.generation,
                    frame.format,
                    Base64.getEncoder().encodeToString(picture.bytes),
                    picture.capturedAt,
                    picture.captureMs,
                )
            }

            "boot" -> slowly(params) { device ->
                runner.run(Commands.boot(device), SLOW_MILLIS)
                settle(device, "Booted")
            }

            "shutdown" -> slowly(params) { device ->
                runner.run(Commands.shutdown(device), SLOW_MILLIS)
                settle(device, "Shutdown")
            }

            "install" -> slowly(params) { device -> install(device, params["path"].asString) }

            "tap", "swipe", "text", "button", "rotate" -> sendInput(method, params)

            else -> synchronized(this) { quickly(method, params) }
        }
    }

    private fun quickly(method: String, params: JsonObject): Any {
        ensureOpen()
        if (method == "status") return status()
        if (method == "log") {
            return mapOf("page" to launchLog.buffer.read(params["cursor"].asLong), "launch" to launchLog.status())
        }
        requireInputIdle()
        val device = chosen(params)
        return when (method) {
            "launch" -> {
                invalidate()
                launchLog.launch(device, bundle(params))
            }

            "terminate" -> {
                launchLog.close()
                // An app that already ended is not an error for someone pressing Stop.
                runCatching { runner.run(Commands.terminate(device, bundle(params)), LIST_MILLIS) }
                mapOf("terminated" to true)
            }

            "report" -> report(device, bundle(params))

            else -> throw IllegalArgumentException("Unknown method")
        }
    }

    private fun chosen(params: JsonObject): String {
        val device = selected?.id ?: throw SimulatorException("NO_DEVICE", "Select a device first")
        if (params["device"].asString != device) {
            throw SimulatorException("DEVICE_CHANGED", "The session selected a different device")
        }
        return device
    }

    private fun slowly(params: JsonObject, work: (String) -> Any): Any {
        val device = synchronized(this) {
            ensureOpen()
            requireInputIdle()
            val chosen = chosen(params)
            if (!slow.compareAndSet(false, true)) {
                throw SimulatorException("BUSY", "Another boot, shutdown or install is still running")
            }
            invalidate()
            chosen
        }
        try {
            return work(device)
        } finally {
            slow.set(false)
        }
    }

    @Synchronized private fun settle(device: String, state: String): Any {
        ensureOpen()
        if (selected?.id == device) selected = selected?.copy(state = state)
        invalidate()
        return status()
    }

    /** The list is read outside the lock, and what it says of the selected device replaces what was known. */
    private fun devices(): List<Device> {
        val found = Devices.parse(runner.run(Commands.list(), LIST_MILLIS))
        synchronized(this) {
            ensureOpen()
            selected = selected?.let { current -> found.find { it.id == current.id } ?: current }
        }
        return found
    }

    /** Switches input on or off while the session runs: the owner decides in the panel, at any time. */
    @Synchronized fun useInput(backend: InputBackend) {
        ensureOpen()
        requireInputIdle()
        invalidate()
        input.close()
        input = backend
    }

    private fun status(): Map<String, Any?> {
        val device = selected?.id
        // Status reads cached geometry and never starts a helper for a hidden panel.
        val scale = device?.takeIf { input.enabled }?.let { runCatching { input.statusScale(it) } }
        return mapOf(
            "device" to selected,
            "generation" to geometry.generation,
            "inputEnabled" to (scale?.isSuccess == true),
            "continuousTouch" to input.continuousTouch,
            "pixelsPerPoint" to (scale?.getOrNull() ?: 1.0),
            "inputNotice" to when {
                !input.enabled -> if (input === NoInput) {
                    "Viewer mode. Switch input on in the menu for a live picture and controls."
                } else {
                    input.notice
                }

                scale?.isFailure == true -> "Viewer mode. ${scale.exceptionOrNull()?.message}"

                else -> input.notice
            },
            "launch" to launchLog.status(),
        )
    }

    private fun select(id: String): Any {
        val device = devices().find { it.id == id }
            ?: throw SimulatorException("NO_DEVICE", "That available iOS device was not found")
        synchronized(this) {
            ensureOpen()
            requireInputIdle()
            if (selected?.id != id) {
                launchLog.close()
                invalidate()
            }
            selected = device
            return status()
        }
    }

    fun screenshot(device: String, format: String = "jpeg"): BinaryFrame {
        val stamp = synchronized(this) {
            checkDevice(device)
            geometry.generation
        }
        try {
            val picture = snapshots.capture(device, format)
            return synchronized(this) {
                checkDevice(device)
                if (stamp !=
                    geometry.generation
                ) {
                    throw SimulatorException("STALE_FRAME", "Screen changed during capture")
                }
                BinaryFrame(device, geometry.record(picture.size), format, picture)
            }
        } catch (failure: Exception) {
            synchronized(this) { if (selected?.id == device && stamp == geometry.generation) invalidate() }
            throw failure
        }
    }

    fun openFrames(device: String): FrameFeed {
        val (serial, backend) = synchronized(this) {
            checkDevice(device)
            requireInputIdle()
            if (slow.get()) throw SimulatorException("BUSY", "Another Simulator operation is still running")
            if (!input.enabled) {
                throw SimulatorException(
                    "INPUT_DISABLED",
                    "Live picture needs the owner's private-input switch",
                )
            }
            stopFrames()
            openingFrames = true
            streamSerial to input
        }
        try {
            val feed = backend.frames(device, bind = { native, displayed ->
                synchronized(this) {
                    if (closed || streamSerial != serial || selected?.id != device) {
                        null
                    } else {
                        geometry.record(native, displayed)
                    }
                }
            }, stopped = {
                synchronized(this) {
                    if (!closed && streamSerial == serial) {
                        geometry.invalidate()
                        touching = false
                        streaming = null
                        streamSerial++
                    }
                }
            })
            synchronized(this) {
                if (closed || selected?.id != device || streamSerial != serial) {
                    feed.close()
                    throw SimulatorException("DEVICE_CHANGED", "Device changed while opening video")
                }
                streaming = feed
                return feed
            }
        } finally {
            synchronized(this) { openingFrames = false }
        }
    }

    private fun ensureOpen() {
        if (closed) throw SimulatorException("CLOSED", "Session is closed")
    }

    private fun checkDevice(device: String) {
        ensureOpen()
        if (selected?.id !=
            device
        ) {
            throw SimulatorException("DEVICE_CHANGED", "The session selected a different device")
        }
    }

    private fun stopFrames() {
        streamSerial++
        touching = false
        streaming?.close()
        streaming = null
    }

    private fun sendInput(method: String, params: JsonObject): Any {
        val (device, backend) = synchronized(this) {
            ensureOpen()
            requireInputIdle()
            if (slow.get()) throw SimulatorException("BUSY", "Another Simulator operation is still running")
            val device = chosen(params)
            if (!input.enabled) NoInput.send(device, method, emptyList())
            // Validate before starting a helper, so malformed requests cannot leave one behind.
            InputArguments.prepare(method, params, geometry, 1.0)
            sendingInput = true
            device to input
        }
        try {
            val density = backend.scale(device)
            val args = synchronized(this) {
                checkDevice(device)
                InputArguments.prepare(method, params, geometry, density).also {
                    if (method == "rotate") invalidate()
                }
            }
            backend.send(device, method, args)
            return mapOf("sent" to true)
        } finally {
            try {
                backend.releaseIdle()
            } finally {
                synchronized(this) { sendingInput = false }
            }
        }
    }

    private fun requireInputIdle() {
        if (sendingInput || touching || openingFrames) {
            throw SimulatorException("BUSY", "Simulator input or stream startup is still running")
        }
    }

    private fun install(device: String, app: String): Any {
        val path = Path.of(app)
        require(path.isAbsolute && app.endsWith(".app") && Files.isDirectory(path)) {
            "Choose an existing absolute .app directory"
        }
        runner.run(Commands.install(device, path.toString()), SLOW_MILLIS)
        return mapOf("installed" to true)
    }

    private fun bundle(params: JsonObject) = params["bundle"].asString.also {
        require(it.matches(Regex("[A-Za-z0-9][A-Za-z0-9.-]{0,254}"))) { "Invalid bundle identifier" }
    }

    private fun report(device: String, bundle: String): Any {
        val root = Path.of(runner.run(Commands.container(device, bundle), 15_000).trim()).toRealPath()
        val report = root.resolve("Documents/as3test.txt").toRealPath()
        require(report.startsWith(root) && Files.size(report) <= 1024 * 1024) { "Invalid test report" }
        return mapOf("text" to Files.readString(report))
    }

    private fun invalidate() {
        geometry.invalidate()
        stopFrames()
    }

    override fun close() {
        closed = true
        synchronized(this) {
            invalidate()
            launchLog.close()
            input.close()
        }
        snapshots.close()
    }

    private companion object {
        const val SLOW_MILLIS = 120_000L
        const val LIST_MILLIS = 15_000L
    }
}
