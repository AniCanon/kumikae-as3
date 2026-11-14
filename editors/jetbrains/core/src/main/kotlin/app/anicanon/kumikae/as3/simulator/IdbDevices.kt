package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions

/** A private companion, input shell and video children. Never attaches to or kills an owner's companion. */
class IdbDevices(
    private val executable: String,
    private val processes: StreamProcesses = OwnedStreamProcesses(),
    private val companionPath: () -> String = {
        listOf(
            Path.of(executable).parent.resolve("idb_companion"),
            Path.of("/opt/homebrew/bin/idb_companion"),
            Path.of("/usr/local/bin/idb_companion"),
        )
            .firstOrNull { Files.isExecutable(it) }?.toString() ?: error("idb_companion was not found")
    },
) : AutoCloseable {
    private var current: DeviceConnection? = null
    private var closed = false

    @Synchronized fun device(id: String): DeviceConnection {
        check(!closed) { "idb is closed" }
        current?.takeIf { it.id == id && it.usable }?.let { return it }
        current?.close()
        current = null
        return DeviceConnection(id, executable, processes, companionPath).also { current = it }
    }

    @Synchronized override fun close() {
        closed = true
        current?.close()
        current = null
        processes.close()
    }
}

class DeviceConnection internal constructor(
    val id: String,
    executable: String,
    private val processes: StreamProcesses,
    companionPath: () -> String,
) : AutoCloseable {
    // macOS socket paths have a small limit; the project and IDE paths are often too long.
    private val directory = Files.createTempDirectory(
        Path.of("/tmp"),
        "kas3-idb-",
        PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")),
    )
    private val socket = directory.resolve("companion.sock")
    private val prefix = listOf(executable, "--companion", socket.toString())
    private var companion: ProcessLines? = null
    private var shell: IdbShell? = null
    private val videos = mutableSetOf<StreamProcess>()
    private var closed = false
    val usable: Boolean get() = !closed && shell?.available == true && companion?.alive == true
    val density: Double
    val geometry: Geometry

    init {
        try {
            companion = ProcessLines(
                processes.start(
                    listOf(
                        companionPath(),
                        "--udid",
                        id,
                        "--grpc-domain-sock",
                        socket.toString(),
                        "--only",
                        "simulator",
                    ),
                ),
            )
            val ready = JsonParser.parseString(companion!!.next()).asJsonObject
            check(ready["grpc_path"]?.asString == socket.toString()) { "idb companion did not open its private socket" }
            shell = IdbShell(processes.start(prefix + listOf("shell", "--no-prompt")))
            val description = JsonParser.parseString(shell!!.describe()).asJsonObject
            check(description["udid"].asString == id) { "idb described a different device" }
            val screen = description.getAsJsonObject("screen_dimensions")
            density = screen["density"].asDouble
            check(density.isFinite() && density in 1.0..10.0) { "Invalid idb screen density" }
            geometry = Geometry(screen["width"].asInt, screen["height"].asInt)
        } catch (failure: Exception) {
            close()
            throw SimulatorException("INPUT_UNAVAILABLE", "Cannot open idb: ${failure.message}")
        }
    }

    fun send(operation: String, args: List<String>) = checkNotNull(shell).send(operation, args)

    @Synchronized fun video(): StreamProcess {
        check(!closed) { "idb connection is closed" }
        val child = processes.start(
            prefix + listOf(
                "video-stream",
                "--format",
                "mjpeg",
                "--compression-quality",
                "0.6",
                "--scale-factor",
                "0.33",
            ),
        )
        videos += child
        return object : StreamProcess by child {
            override fun close() {
                synchronized(this@DeviceConnection) { videos.remove(child) }
                child.close()
            }
        }
    }

    @Synchronized override fun close() {
        if (closed) return
        closed = true
        videos.forEach { it.close() }
        videos.clear()
        shell?.close()
        companion?.close()
        Files.deleteIfExists(socket)
        Files.deleteIfExists(directory)
    }
}
