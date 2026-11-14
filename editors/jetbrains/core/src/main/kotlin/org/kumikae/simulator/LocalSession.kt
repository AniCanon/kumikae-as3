package org.kumikae.simulator

import java.nio.file.Files
import java.nio.file.Path

class LocalSession(parent: Path) : AutoCloseable {
    private val directory = SessionDirectory.create(parent)
    private val runner = ProcessRunner()
    private val session: SimulatorSession
    val server: LocalServer

    init {
        try {
            session = SimulatorSession(directory, runner, IdbInput.fromEnvironment(runner))
            server = LocalServer(directory, Protocol(session::dispatch))
        } catch (e: Exception) {
            Files.deleteIfExists(directory.resolve("session.json"))
            Files.deleteIfExists(directory)
            throw e
        }
    }

    override fun close() {
        try { server.close() }
        finally {
            try { session.close() }
            finally { Files.deleteIfExists(directory) }
        }
    }
}
