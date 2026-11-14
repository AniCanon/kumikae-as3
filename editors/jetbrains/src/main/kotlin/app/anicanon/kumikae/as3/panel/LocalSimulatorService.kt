package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.ApiClient
import app.anicanon.kumikae.as3.simulator.LocalSession
import com.google.gson.JsonElement
import java.nio.file.Path

/**
 * The window's own Simulator service: the same local session an agent reaches through the endpoint file.
 * The window talks to it over the same HTTP interface, so both clients see one selection and one console.
 */
internal class LocalSimulatorService(
    private val stateDirectory: Path,
    private val closeLater: (() -> Unit) -> Unit = { work ->
        Thread(work, "Kumikae AS3 Simulator shutdown").apply { isDaemon = true }.start()
    },
) : SimulatorService {
    private val lock = Any()
    private var session: LocalSession? = null
    private var client: ApiClient? = null
    private var closed = false

    override fun start(): String {
        synchronized(lock) {
            check(!closed) { CLOSED }
            session?.let { return it.server.endpointFile.toString() }
        }
        val opened = LocalSession(stateDirectory)
        synchronized(lock) {
            // The window may close while the session is still being created on the worker.
            if (closed) {
                opened.close()
                error(CLOSED)
            }
            session = opened
            client = ApiClient(opened.server.endpoint)
        }
        return opened.server.endpointFile.toString()
    }

    override fun call(method: String, params: Map<String, Any>): JsonElement {
        val current = synchronized(lock) {
            check(!closed) { CLOSED }
            client
        }
        return checkNotNull(current) { "The local Simulator service is still starting." }.call(method, params)
    }

    override fun enableInput(): Boolean {
        val current = synchronized(lock) {
            check(!closed) { CLOSED }
            session
        }
        return checkNotNull(current) { "The local Simulator service is still starting." }.enableInput()
    }

    override fun close() {
        val opened = synchronized(lock) {
            closed = true
            client = null
            session.also { session = null }
        } ?: return
        // Closing waits for the session's running command, which can be a two-minute boot.
        closeLater { opened.close() }
    }

    private companion object {
        const val CLOSED = "The local Simulator service is closed."
    }
}
