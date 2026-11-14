package app.anicanon.kumikae.as3.panel

import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LocalSimulatorServiceTest {
    @TempDir lateinit var directory: Path

    private fun service() = LocalSimulatorService(directory) { it() }

    @Test fun `starting publishes one endpoint file and answers calls until closed`() {
        val service = service()
        val endpoint = Path.of(service.start())
        assertTrue(endpoint.startsWith(directory) && Files.isRegularFile(endpoint))
        assertEquals(endpoint.toString(), service.start())
        assertFalse(service.call("status").asJsonObject["inputEnabled"].asBoolean)
        service.close()
        assertFalse(Files.exists(endpoint))
        assertFailsWith<IllegalStateException> { service.call("status") }
    }

    @Test fun `a call before the service starts is refused`() {
        assertFailsWith<IllegalStateException> { service().call("status") }
    }

    @Test fun `a service closed before it starts never opens a session`() {
        val service = service()
        service.close()
        assertFailsWith<IllegalStateException> { service.start() }
        assertEquals(0, Files.list(directory).use { it.count() })
    }

    @Test fun `closing hands the slow session shutdown to the supplied runner`() {
        var deferred: (() -> Unit)? = null
        val service = LocalSimulatorService(directory) { deferred = it }
        val endpoint = Path.of(service.start())
        service.close()
        assertTrue(Files.exists(endpoint))
        checkNotNull(deferred).invoke()
        assertFalse(Files.exists(endpoint))
    }
}
