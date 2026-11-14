package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import java.nio.file.Files
import java.nio.file.Path
import java.util.Base64
import java.util.concurrent.atomic.AtomicBoolean
import javax.imageio.ImageIO

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
    private var geometry: Geometry? = null
    private var generation = 0L

    @Volatile private var closed = false

    // Held by the one boot, install or shutdown that is running. They take up to two minutes, so they run
    // outside the session's lock: status, the list, the log and pictures still answer meanwhile.
    private val slow = AtomicBoolean()

    fun dispatch(method: String, params: JsonObject): Any {
        if (closed) throw SimulatorException("CLOSED", "Session is closed")
        return when (method) {
            "devices" -> devices()

            "select" -> select(params["device"].asString)

            "boot" -> slowly(params) { device ->
                runner.run(Commands.boot(device), SLOW_MILLIS)
                settle(device, "Booted")
            }

            "shutdown" -> slowly(params) { device ->
                runner.run(Commands.shutdown(device), SLOW_MILLIS)
                settle(device, "Shutdown")
            }

            "install" -> slowly(params) { device -> install(device, params["path"].asString) }

            else -> synchronized(this) { quickly(method, params) }
        }
    }

    private fun quickly(method: String, params: JsonObject): Any {
        if (method == "status") return status()
        if (method == "log") {
            return mapOf("page" to launchLog.buffer.read(params["cursor"].asLong), "launch" to launchLog.status())
        }
        val device = chosen(params)
        return when (method) {
            "screenshot" -> capture(device, params["format"]?.asString ?: "png")

            "launch" -> {
                invalidate()
                launchLog.launch(device, bundle(params))
            }

            "report" -> report(device, bundle(params))

            "tap", "swipe", "text", "button", "rotate" -> input(device, method, params)

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
        val device = synchronized(this) { chosen(params).also { invalidate() } }
        if (!slow.compareAndSet(false, true)) {
            throw SimulatorException("BUSY", "Another boot, shutdown or install is still running")
        }
        try {
            return work(device)
        } finally {
            slow.set(false)
        }
    }

    @Synchronized private fun settle(device: String, state: String): Any {
        if (selected?.id == device) selected = selected?.copy(state = state)
        invalidate()
        return status()
    }

    /** The list is read outside the lock, and what it says of the selected device replaces what was known. */
    private fun devices(): List<Device> {
        val found = Devices.parse(runner.run(Commands.list(), LIST_MILLIS))
        synchronized(this) { selected = selected?.let { current -> found.find { it.id == current.id } ?: current } }
        return found
    }

    /** Switches input on or off while the session runs: the owner decides in the panel, at any time. */
    fun useInput(backend: InputBackend) {
        input = backend
    }

    private fun status(): Map<String, Any?> {
        val device = selected?.id
        // A device idb cannot describe stays in viewer mode, with the reason shown.
        val scale = device?.takeIf { input.enabled }?.let { runCatching { input.scale(it) } }
        return mapOf(
            "device" to selected,
            "generation" to generation,
            "inputEnabled" to (scale?.isSuccess == true),
            "pixelsPerPoint" to (scale?.getOrNull() ?: 1.0),
            "inputNotice" to when {
                !input.enabled -> "Viewer mode. Input can be switched on in the menu; it needs idb."
                scale?.isFailure == true -> "Viewer mode. ${scale.exceptionOrNull()?.message}"
                else -> "Input is on, through idb."
            },
            "launch" to launchLog.status(),
        )
    }

    private fun select(id: String): Any {
        val device = devices().find { it.id == id }
            ?: throw SimulatorException("NO_DEVICE", "That available iOS device was not found")
        synchronized(this) {
            if (selected?.id != id) {
                launchLog.close()
                invalidate()
            }
            selected = device
            return status()
        }
    }

    private fun capture(device: String, format: String): Frame {
        require(format in setOf("png", "jpeg")) { "format must be png or jpeg" }
        val file = Files.createTempFile(directory, "frame-", ".$format")
        val started = System.nanoTime()
        try {
            runner.run(Commands.screenshot(device, file.toString(), format), 15_000)
            require(Files.size(file) in 1..32L * 1024 * 1024) { "Invalid screenshot size" }
            val size = ImageIO.createImageInputStream(file.toFile()).use { stream ->
                val readers = ImageIO.getImageReaders(stream)
                require(readers.hasNext()) { "Screenshot is not an image" }
                val reader = readers.next()
                try {
                    reader.input = stream
                    Geometry(reader.getWidth(0), reader.getHeight(0)).also {
                        require(it.width.toLong() * it.height <= 40_000_000) { "Screenshot is too large" }
                    }
                } finally {
                    reader.dispose()
                }
            }
            if (geometry != size) {
                generation++
                geometry = size
            }
            return Frame(
                device,
                size.width,
                size.height,
                generation,
                format,
                Base64.getEncoder().encodeToString(Files.readAllBytes(file)),
                System.currentTimeMillis(),
                (System.nanoTime() - started) / 1_000_000,
            )
        } catch (e: Exception) {
            invalidate()
            throw e
        } finally {
            Files.deleteIfExists(file)
        }
    }

    private fun input(device: String, method: String, p: JsonObject): Any {
        if (!input.enabled) NoInput.send(device, method, emptyList())
        val args = when (method) {
            "tap", "swipe" -> gesture(device, method, p)

            "text" -> listOf(
                p["text"].asString.also { text ->
                    require(text.isNotEmpty() && text.all { it.code in 32..126 }) { "Text must be printable ASCII" }
                },
            )

            "button" -> listOf(p["button"].asString.also { require(it in setOf("home", "lock")) }.uppercase())

            "rotate" -> listOf(
                p["orientation"].asString.also {
                    require(it in setOf("portrait", "portrait_upside_down", "landscape_left", "landscape_right"))
                }.uppercase(),
            )

            else -> error("Unknown input operation")
        }
        // Invalidate even if rotation fails: its outcome can be uncertain after a timeout.
        if (method == "rotate") invalidate()
        input.send(device, method, args)
        return mapOf("sent" to true)
    }

    private fun gesture(device: String, method: String, p: JsonObject): List<String> {
        val current = geometry ?: throw SimulatorException("STALE_FRAME", "Take a new screenshot before input")
        if (p["generation"].asLong != generation || p["width"].asLong != current.width.toLong() ||
            p["height"].asLong != current.height.toLong()
        ) {
            throw SimulatorException("STALE_FRAME", "Screen geometry changed; take a new screenshot")
        }
        val scale = input.scale(device)
        val start = current.point(p["x"].asDouble, p["y"].asDouble, scale)
        if (method == "tap") return listOf(start.x.toString(), start.y.toString())
        val end = current.point(p["endX"].asDouble, p["endY"].asDouble, scale)
        val duration = p["durationMs"].asLong.also { require(it in 50..5000) { "Swipe duration must be 50–5000ms" } }
        return listOf(
            start.x.toString(),
            start.y.toString(),
            end.x.toString(),
            end.y.toString(),
            "--duration",
            (duration / 1000.0).toString(),
        )
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
        geometry = null
        generation++
    }

    override fun close() {
        closed = true
        synchronized(this) {
            invalidate()
            launchLog.close()
        }
    }

    private companion object {
        const val SLOW_MILLIS = 120_000L
        const val LIST_MILLIS = 15_000L
    }
}
