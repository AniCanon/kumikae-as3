package app.anicanon.kumikae.as3.panel

import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions
import java.util.concurrent.TimeUnit

/** Installs idb on this Mac. It blocks for as long as the install takes and returns what it printed. */
internal fun interface IdbInstaller {
    fun install(): String
}

internal class IdbInstallFailed(message: String, val output: String) : Exception(message)

/** Runs the install script that ships with the plugin: the same one a person can run from `tools/`. */
internal class ScriptIdbInstaller(
    private val script: () -> String = ::shipped,
    private val home: Path = Path.of(System.getProperty("user.home")),
) : IdbInstaller {
    override fun install(): String {
        val file = Files.createTempFile(
            "kumikae-install-idb",
            ".sh",
            PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")),
        )
        try {
            Files.writeString(file, script())
            val process = ProcessBuilder("/bin/sh", file.toString()).redirectErrorStream(true).apply {
                // An IDE started from the Dock has a short search path: without these the script finds
                // neither Homebrew nor the tools it installs.
                val path = environment()["PATH"].orEmpty()
                environment()["PATH"] = "/opt/homebrew/bin:/usr/local/bin:$home/.local/bin:$path"
                environment()["HOME"] = home.toString()
                environment()["HOMEBREW_NO_AUTO_UPDATE"] = "1"
            }.start()
            process.outputStream.close()
            val output = process.inputStream.bufferedReader().readText()
            if (!process.waitFor(LIMIT_MINUTES, TimeUnit.MINUTES)) {
                process.destroyForcibly()
                throw IdbInstallFailed("The install did not finish in $LIMIT_MINUTES minutes.", output)
            }
            if (process.exitValue() != 0) {
                throw IdbInstallFailed("The install stopped with status ${process.exitValue()}.", output)
            }
            return output
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IdbInstallFailed("The install was stopped.", "")
        } finally {
            Files.deleteIfExists(file)
        }
    }

    companion object {
        private const val LIMIT_MINUTES = 15L
        private const val RESOURCE = "/install-idb.sh"

        fun shipped(): String = checkNotNull(ScriptIdbInstaller::class.java.getResourceAsStream(RESOURCE)) {
            "The plugin was built without its install script."
        }.bufferedReader().use { it.readText() }
    }
}
