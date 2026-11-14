package app.anicanon.kumikae.as3.simulator

import java.nio.file.Files
import java.nio.file.Path

class LocalSession(
    parent: Path,
    private val nativeExecutable: Path? = System.getenv("KUMIKAE_SIMULATOR_NATIVE")?.let(Path::of),
) : AutoCloseable {
    private val directory = SessionDirectory.create(parent)
    private val runner = ProcessRunner()
    private val session: SimulatorSession
    val server: LocalServer

    init {
        try {
            session = SimulatorSession(directory, runner)
            server = LocalServer(directory, Protocol(session::dispatch))
            if (System.getenv("KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT") == "1") enableInput()
        } catch (e: Exception) {
            Files.deleteIfExists(directory.resolve("session.json"))
            Files.deleteIfExists(directory)
            throw e
        }
    }

    /** The owner's existing private-input switch covers the native helper and the idb fallback. */
    fun enableInput(): Boolean {
        val candidates = mutableListOf<Pair<String, InputBackend>>()
        nativeExecutable?.takeIf { Files.isExecutable(it) && !Files.isDirectory(it) }?.let {
            candidates += "native helper" to NativeInput(it.toString(), directory, OwnedStreamProcesses(4000))
        }
        (System.getenv("KUMIKAE_SIMULATOR_IDB") ?: IdbInput.find())?.let {
            candidates +=
                "idb" to IdbInput(it, runner, IdbDevices(it))
        }
        if (candidates.isEmpty()) return false
        session.useInput(FallbackInput(candidates))
        return true
    }

    fun touch(device: String, stamp: Long, displayed: Geometry, phase: String, at: Pixel) =
        session.touch(device, stamp, displayed, phase, at)

    fun openFrames(device: String): FrameFeed = session.openFrames(device)

    fun screenshot(device: String): BinaryFrame = session.screenshot(device)

    override fun close() {
        try {
            server.close()
        } finally {
            try {
                session.close()
            } finally {
                Files.deleteIfExists(directory)
            }
        }
    }
}
