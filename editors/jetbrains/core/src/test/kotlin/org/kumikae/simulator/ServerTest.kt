package org.kumikae.simulator

import org.junit.Assert.*
import org.junit.Test
import java.net.HttpURLConnection
import java.net.URI
import java.nio.file.Files

class ServerTest {
    @Test fun `server requires its token and removes the endpoint on close`() {
        val dir = SessionDirectory.create(Files.createTempDirectory("simulator-parent"))
        val server = LocalServer(dir, Protocol { _, _ -> mapOf("ok" to true) })
        val descriptor = server.endpoint
        val connection = URI("http://127.0.0.1:${descriptor.port}/v1").toURL().openConnection() as HttpURLConnection
        connection.requestMethod = "POST"
        assertEquals(401, connection.responseCode)
        connection.disconnect()
        assertTrue(ApiClient(descriptor).call("devices").asJsonObject["ok"].asBoolean)
        assertEquals("rw-------", java.nio.file.attribute.PosixFilePermissions.toString(
            Files.getPosixFilePermissions(dir.resolve("session.json"))))
        server.close()
        assertFalse(Files.exists(dir.resolve("session.json")))
        Files.delete(dir)
        Files.delete(dir.parent)
    }
}
