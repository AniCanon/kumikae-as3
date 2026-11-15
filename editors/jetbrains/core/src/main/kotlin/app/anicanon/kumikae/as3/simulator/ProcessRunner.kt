package app.anicanon.kumikae.as3.simulator

import java.io.InputStream
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

fun interface CommandRunner {
    fun run(command: List<String>, timeoutMs: Long): String

    fun runWithInput(command: List<String>, timeoutMs: Long, input: ByteArray): String =
        throw SimulatorException("PROCESS_FAILED", "Command input is unavailable")
}

class ProcessRunner : CommandRunner {
    override fun run(command: List<String>, timeoutMs: Long): String = execute(command, timeoutMs, byteArrayOf())

    override fun runWithInput(command: List<String>, timeoutMs: Long, input: ByteArray): String {
        require(input.size <= 16384) { "Command input exceeds 16384 bytes" }
        return execute(command, timeoutMs, input)
    }

    private fun execute(command: List<String>, timeoutMs: Long, input: ByteArray): String {
        val process = try {
            ProcessBuilder(command).start()
        } catch (
            e: java.io.IOException,
        ) {
            throw SimulatorException("PROCESS_FAILED", "Cannot start ${command.first()}: ${e.message}")
        }
        val io = Executors.newFixedThreadPool(3) { work ->
            Thread(work, "kumikae-command-pipe").apply { isDaemon = true }
        }
        val out = drain(process.inputStream, io)
        val err = drain(process.errorStream, io)
        val writer = CompletableFuture.runAsync({ process.outputStream.use { it.write(input) } }, io)
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
            writer.get(2, TimeUnit.SECONDS)
            return output
        } finally {
            try {
                process.descendants().forEach { if (it.isAlive) it.destroyForcibly() }
                if (process.isAlive) process.destroyForcibly()
                process.waitFor(1, TimeUnit.SECONDS)
            } finally {
                try {
                    process.inputStream.close()
                    process.errorStream.close()
                    process.outputStream.close()
                } finally {
                    io.shutdownNow()
                    check(io.awaitTermination(2, TimeUnit.SECONDS)) { "Command pipe workers did not stop" }
                }
            }
        }
    }

    private fun drain(stream: InputStream, io: ExecutorService) = CompletableFuture.supplyAsync({
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
    }, io)
}
