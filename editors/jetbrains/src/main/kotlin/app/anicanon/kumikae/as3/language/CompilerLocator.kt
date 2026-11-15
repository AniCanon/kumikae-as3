package app.anicanon.kumikae.as3.language

import java.nio.file.Files
import java.nio.file.Path

/** Finds `kas3c`, the compiler that is also the language server. */
internal object CompilerLocator {
    /**
     * In order: the path the person set; a build of the compiler in the project or a folder above it (the
     * compiler's own repository); the one the plugin carries; the search path; the home folder's own bin.
     * On Windows the program is `kas3c.exe` and the search path is split at `;`.
     */
    fun find(
        project: Path,
        configured: String,
        searchPath: String,
        home: Path,
        bundled: Path? = null,
        windows: Boolean = System.getProperty("os.name").startsWith("Windows"),
    ): Path? {
        val name = if (windows) "kas3c.exe" else "kas3c"
        val set = configured.trim().takeIf { it.isNotEmpty() }?.let(Path::of)
        val above = generateSequence(project.toAbsolutePath()) { it.parent }.flatMap {
            sequenceOf(it.resolve("compiler/build/$name"), it.resolve("build/$name"))
        }
        // A part of the search path that is not a path at all is passed over, like a relative one.
        val searched = searchPath.split(if (windows) ';' else ':')
            .mapNotNull { part -> runCatching { Path.of(part) }.getOrNull()?.takeIf { it.isAbsolute } }
            .map { it.resolve(name) }
        val known = if (windows) {
            emptyList()
        } else {
            listOf(Path.of("/opt/homebrew/bin", name), Path.of("/usr/local/bin", name))
        }
        // A Path is itself a list of its parts: it is wrapped, or `+` would add "home", ".local", "bin".
        val last = sequenceOf(home.resolve(".local/bin/$name"))
        // The one the plugin carries comes after a build of the project's own: that one is being worked on.
        val carried = sequenceOf(bundled?.resolve(name)).filterNotNull()
        val candidates = sequenceOf(set).filterNotNull() + above + carried + searched + known + last
        return candidates.firstOrNull { Files.isExecutable(it) && !Files.isDirectory(it) }
    }
}
