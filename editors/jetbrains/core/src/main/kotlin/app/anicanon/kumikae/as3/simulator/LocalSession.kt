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
        var createdSession: SimulatorSession? = null
        var createdServer: LocalServer? = null
        try {
            createdSession = SimulatorSession(directory, runner)
            createdServer = LocalServer(directory, Protocol(createdSession::dispatch))
            session = createdSession
            server = createdServer
            if (System.getenv("KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT") == "1") enableInput()
        } catch (e: Exception) {
            runCatching { createdServer?.close() }.onFailure(e::addSuppressed)
            runCatching { createdSession?.close() }.onFailure(e::addSuppressed)
            Files.deleteIfExists(directory.resolve("session.json"))
            Files.deleteIfExists(directory)
            throw e
        }
    }

    /** The owner's private-input switch enables the bundled native helper. */
    fun enableInput(): Boolean {
        val executable = nativeExecutable?.takeIf { Files.isExecutable(it) && !Files.isDirectory(it) }
            ?: return false
        session.useInput(
            NativeInput(executable.toString(), directory, OwnedStreamProcesses(4000), SimctlPasteboard(runner)),
        )
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
