package app.anicanon.kumikae.as3

import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions
import java.util.concurrent.TimeUnit

internal class ScriptFailed(message: String, val output: String) : Exception(message)

/**
 * Runs one of the shell scripts the plugin carries (the same ones a person can run from `tools/`), with
 * what it prints passed on line by line as it comes.
 */
internal class ShippedScript(
    private val text: () -> String,
    private val home: Path = Path.of(System.getProperty("user.home")),
    private val limitMinutes: Long = DEFAULT_LIMIT_MINUTES,
) {
    constructor(resource: String) : this({ read(resource) })

    /** Everything the script printed. Throws ScriptFailed, with what it printed, if it did not end well. */
    fun run(arguments: List<String> = emptyList(), onLine: (String) -> Unit = {}): String {
        val file = Files.createTempFile(
            "kumikae-script",
            ".sh",
            PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")),
        )
        try {
            Files.writeString(file, text())
            val process = ProcessBuilder(
                listOf("/bin/sh", file.toString()) + arguments,
            ).redirectErrorStream(true).apply {
                // An IDE started from the Dock has a short search path: without these a script finds
                // neither Homebrew nor the tools it installs.
                val path = environment()["PATH"].orEmpty()
                environment()["PATH"] = "/opt/homebrew/bin:/usr/local/bin:$home/.local/bin:$path"
                environment()["HOME"] = home.toString()
                environment()["HOMEBREW_NO_AUTO_UPDATE"] = "1"
            }.start()
            process.outputStream.close()
            val output = StringBuffer()
            val reader = Thread {
                try {
                    process.inputStream.bufferedReader().use { input ->
                        input.forEachLine { line ->
                            output.append(line).append('\n')
                            onLine(line)
                        }
                    }
                } catch (_: IOException) {
                    // Process teardown closes the stream.
                }
            }.apply {
                isDaemon = true
                start()
            }
            try {
                if (!process.waitFor(limitMinutes, TimeUnit.MINUTES)) {
                    throw ScriptFailed("It did not finish in $limitMinutes minutes.", output.toString())
                }
                reader.join(READER_JOIN_MILLIS)
            } finally {
                if (process.isAlive) {
                    process.descendants().use { children -> children.forEach { it.destroyForcibly() } }
                    process.destroy()
                    if (!process.waitFor(5, TimeUnit.SECONDS)) process.destroyForcibly().waitFor()
                }
                runCatching { process.inputStream.close() }
                reader.join(READER_JOIN_MILLIS)
            }
            if (process.exitValue() != 0) {
                throw ScriptFailed("It stopped with status ${process.exitValue()}.", output.toString())
            }
            return output.toString()
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
            throw ScriptFailed("It was stopped.", "")
        } finally {
            Files.deleteIfExists(file)
        }
    }

    companion object {
        private const val DEFAULT_LIMIT_MINUTES = 15L
        private const val READER_JOIN_MILLIS = 5_000L

        fun read(resource: String): String = checkNotNull(ShippedScript::class.java.getResourceAsStream(resource)) {
            "The plugin was built without $resource."
        }.bufferedReader().use { it.readText() }
    }
}
