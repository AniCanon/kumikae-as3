package app.anicanon.kumikae.as3.simulator

import com.google.gson.Gson
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardOpenOption
import java.nio.file.attribute.PosixFilePermissions
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit

data class Endpoint(val version: Int, val port: Int, val token: String)

object SessionDirectory {
    fun create(parent: Path): Path {
        Files.createDirectories(parent)
        return Files.createTempDirectory(
            parent,
            "simulator-",
            PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")),
        )
    }
}

class LocalServer(private val directory: Path, private val protocol: Protocol) : AutoCloseable {
    private val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 8)
    private val executor = ThreadPoolExecutor(
        2,
        2,
        0,
        TimeUnit.SECONDS,
        ArrayBlockingQueue(4),
        { work -> Thread(work, "kumikae-simulator-api").apply { isDaemon = true } },
        ThreadPoolExecutor.AbortPolicy(),
    )
    val endpoint = Endpoint(
        1,
        server.address.port,
        Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(32).also { SecureRandom().nextBytes(it) }),
    )
    val endpointFile: Path = directory.resolve("session.json")

    init {
        try {
            server.executor = executor
            server.createContext("/v1", ::handle)
            Files.newByteChannel(
                endpointFile,
                setOf(StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE),
                PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")),
            ).use {
                val bytes = java.nio.ByteBuffer.wrap(Gson().toJson(endpoint).toByteArray(Charsets.UTF_8))
                while (bytes.hasRemaining()) it.write(bytes)
            }
            server.start()
        } catch (e: Exception) {
            server.stop(0)
            executor.shutdownNow()
            throw e
        }
    }

    private fun handle(exchange: HttpExchange) {
        exchange.use {
            val expected = "Bearer ${endpoint.token}".toByteArray(Charsets.UTF_8)
            val supplied = (it.requestHeaders.getFirst("Authorization") ?: "").toByteArray(Charsets.UTF_8)
            if (!MessageDigest.isEqual(expected, supplied) || it.requestHeaders.containsKey("Origin")) {
                reply(it, 401, "{\"error\":\"Unauthorized\"}")
                return
            }
            if (it.requestURI.toString() != "/v1") {
                reply(it, 404, "{}")
                return
            }
            if (it.requestMethod != "POST") {
                reply(it, 405, "{}")
                return
            }
            val body = it.requestBody.readNBytes(Protocol.MAX_REQUEST + 1)
            if (body.size > Protocol.MAX_REQUEST) {
                reply(it, 413, "{}")
                return
            }
            reply(it, 200, protocol.respond(body.toString(Charsets.UTF_8)))
        }
    }

    private fun reply(exchange: HttpExchange, status: Int, text: String) {
        val bytes = text.toByteArray(Charsets.UTF_8)
        exchange.responseHeaders.set("Content-Type", "application/json; charset=utf-8")
        exchange.responseHeaders.set("Cache-Control", "no-store")
        exchange.sendResponseHeaders(status, bytes.size.toLong())
        exchange.responseBody.write(bytes)
    }

    override fun close() {
        server.stop(0)
        executor.shutdownNow()
        Files.deleteIfExists(endpointFile)
    }
}
