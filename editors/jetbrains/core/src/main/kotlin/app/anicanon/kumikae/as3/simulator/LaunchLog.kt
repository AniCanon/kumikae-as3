package app.anicanon.kumikae.as3.simulator

import java.io.InputStream
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class LaunchLog(private val start: (List<String>) -> Process = { ProcessBuilder(it).start() }) : AutoCloseable {
    val buffer = LogBuffer()
    private var process: Process? = null
    private var bundle: String? = null
    private val readers = mutableListOf<Thread>()

    fun launch(device: String, app: String): Map<String, Any?> {
        close()
        bundle = app
        buffer.append("session", "Launching $app\n")
        val child = try {
            start(Commands.launch(device, app))
        } catch (
            e: java.io.IOException,
        ) {
            throw SimulatorException("PROCESS_FAILED", "Cannot start console launcher: ${e.message}")
        }
        process = child
        child.outputStream.close()
        pump(child.inputStream, "stdout")
        pump(child.errorStream, "stderr")
        return status() + ("state" to "starting")
    }

    fun status(): Map<String, Any?> = mapOf(
        "bundle" to bundle,
        "running" to (process?.isAlive == true),
        "exitCode" to process?.let { if (it.isAlive) null else it.exitValue() },
    )

    private fun pump(stream: InputStream, label: String) {
        readers.add(
            thread(name = "kumikae-simulator-$label", isDaemon = true) {
                try {
                    stream.reader(Charsets.UTF_8).use { reader ->
                        val chars = CharArray(4096)
                        while (true) {
                            val count = reader.read(chars)
                            if (count < 0) break
                            buffer.append(label, String(chars, 0, count))
                        }
                    }
                } catch (_: java.io.IOException) {
                    // Closing the session interrupts a pipe read.
                }
            },
        )
    }

    override fun close() {
        process?.let {
            if (it.isAlive) {
                it.destroy()
                if (!it.waitFor(500, TimeUnit.MILLISECONDS)) it.destroyForcibly()
                it.waitFor(500, TimeUnit.MILLISECONDS)
            }
            it.inputStream.close()
            it.errorStream.close()
        }
        process = null
        bundle = null
        readers.forEach { it.join(1000) }
        readers.clear()
    }
}
