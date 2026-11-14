package app.anicanon.kumikae.as3.simulator

import java.io.InputStream
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit

fun interface CommandRunner {
    fun run(command: List<String>, timeoutMs: Long): String
}

class ProcessRunner : CommandRunner {
    override fun run(command: List<String>, timeoutMs: Long): String {
        val process = try {
            ProcessBuilder(command).start()
        } catch (
            e: java.io.IOException,
        ) {
            throw SimulatorException("PROCESS_FAILED", "Cannot start ${command.first()}: ${e.message}")
        }
        process.outputStream.close()
        val out = drain(process.inputStream)
        val err = drain(process.errorStream)
        try {
            if (!process.waitFor(timeoutMs, TimeUnit.MILLISECONDS)) {
                throw SimulatorException("TIMEOUT", "Command exceeded ${timeoutMs}ms")
            }
            val output = out.get(2, TimeUnit.SECONDS)
            val errors = err.get(2, TimeUnit.SECONDS)
            if (process.exitValue() != 0) {
                throw SimulatorException(
                    "PROCESS_FAILED",
                    errors.take(4096).ifBlank { output.take(4096) }
                        .ifBlank { "Command exited ${process.exitValue()}" },
                )
            }
            return output
        } finally {
            process.descendants().forEach { if (it.isAlive) it.destroyForcibly() }
            if (process.isAlive) process.destroyForcibly()
            process.waitFor(1, TimeUnit.SECONDS)
            process.inputStream.close()
            process.errorStream.close()
        }
    }

    private fun drain(stream: InputStream) = CompletableFuture.supplyAsync {
        stream.use {
            val bytes = java.io.ByteArrayOutputStream()
            val buffer = ByteArray(8192)
            while (true) {
                val count = it.read(buffer)
                if (count < 0) break
                // Keep draining a noisy child so it cannot fill the pipe and deadlock.
                val keep = minOf(count, 4 * 1024 * 1024 - bytes.size())
                if (keep > 0) bytes.write(buffer, 0, keep)
            }
            bytes.toString(Charsets.UTF_8)
        }
    }
}
