package app.anicanon.kumikae.as3.project

import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.HexFormat
import java.util.concurrent.TimeUnit

internal data class GodotInstallation(val executable: Path, val version: String)

internal interface GodotSetup {
    fun detect(): GodotInstallation?
    fun choose(path: Path): GodotInstallation
    fun install(): GodotInstallation
}

internal class LocalGodotSetup(
    private val candidates: () -> List<Path>,
    private val directory: Path,
    private val download: (Path) -> Unit = ::downloadGodot,
) : GodotSetup {
    override fun detect(): GodotInstallation? = candidates().firstNotNullOfOrNull(::inspect)

    override fun choose(path: Path): GodotInstallation =
        requireNotNull(inspect(path)) { "Choose a stable Godot 4.7 app or executable." }

    override fun install(): GodotInstallation {
        val app = directory.resolve("Godot.app")
        inspect(app)?.let { return it }
        check(!Files.exists(app)) { "An unusable Godot.app exists in $directory. Move it aside or choose another app." }
        Files.createDirectories(directory)
        val stage = Files.createTempDirectory(directory, ".install-")
        try {
            val archive = stage.resolve("godot.zip")
            download(archive)
            val hash = MessageDigest.getInstance("SHA-256")
            Files.newInputStream(archive).use { input ->
                val buffer = ByteArray(65536)
                var n = input.read(buffer)
                while (n > 0) {
                    if (Thread.interrupted()) throw InterruptedException()
                    hash.update(buffer, 0, n)
                    n = input.read(buffer)
                }
            }
            check(HexFormat.of().formatHex(hash.digest()) == DIGEST) {
                "Download verification failed. Try installing again."
            }
            command(listOf("/usr/bin/ditto", "-x", "-k", archive.toString(), stage.toString()), 120, "Unpacking Godot")
            val staged = stage.resolve("Godot.app")
            val engine = choose(staged)
            check(engine.version == VERSION) { "The downloaded Godot version is different from $VERSION." }
            try {
                Files.move(staged, app, StandardCopyOption.ATOMIC_MOVE)
            } catch (failure: IOException) {
                // Another wizard may have completed installation while this one was downloading.
                return inspect(app) ?: throw failure
            }
            return engine.copy(executable = app.resolve(BINARY))
        } finally {
            Files.walk(stage).use { paths -> paths.sorted(Comparator.reverseOrder()).forEach(Files::deleteIfExists) }
        }
    }

    private fun inspect(path: Path): GodotInstallation? {
        val exe = if (path.fileName?.toString()?.endsWith(".app") == true) path.resolve(BINARY) else path
        if (!Files.isRegularFile(exe) || !Files.isExecutable(exe)) return null
        val version = try {
            command(listOf(exe.toString(), "--version"), 6, "Checking Godot")
        } catch (_: IOException) {
            return null
        }
        val match = STABLE.matchEntire(version.trim()) ?: return null
        return GodotInstallation(exe, match.groupValues[1])
    }

    companion object {
        const val VERSION = "4.7.2"
        const val BINARY = "Contents/MacOS/Godot"
        private val STABLE = Regex("""(4\.7(?:\.\d+)?)\.stable(?:\.[a-zA-Z0-9_-]+)*""")
        private const val DIGEST = "c58a24e31d720be9d62f60cb5627c4e695fb72f21b0cfe1bc9ccaa9a3b3ba63e"
        private const val URL = "https://github.com/godotengine/godot-builds/releases/download/" +
            "$VERSION-stable/Godot_v$VERSION-stable_macos.universal.zip"

        private fun downloadGodot(to: Path) {
            command(
                listOf(
                    "/usr/bin/curl", "--disable", "--fail", "--silent", "--show-error", "--location",
                    "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "20",
                    "--output", to.toString(), URL,
                ),
                600,
                "Downloading Godot",
            )
        }

        private fun command(args: List<String>, seconds: Long, label: String): String {
            val process = ProcessBuilder(args).redirectErrorStream(true).start()
            try {
                if (!process.waitFor(seconds, TimeUnit.SECONDS)) throw IOException("$label timed out.")
                val output = process.inputStream.use { it.readNBytes(2048).decodeToString().trim() }
                if (process.exitValue() != 0) throw IOException("$label failed. $output")
                return output
            } finally {
                if (process.isAlive) process.destroyForcibly().waitFor()
            }
        }
    }
}
