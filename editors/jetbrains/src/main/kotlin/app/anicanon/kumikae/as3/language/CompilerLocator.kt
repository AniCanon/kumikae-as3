package app.anicanon.kumikae.as3.language

import java.nio.file.Files
import java.nio.file.Path

/** Finds `kas3c`, the compiler that is also the language server. */
internal object CompilerLocator {
    private const val NAME = "kas3c"

    /**
     * In order: the path the person set; a build of the compiler in the project or a folder above it (the
     * compiler's own repository); the search path; the home folder's own bin.
     */
    fun find(project: Path, configured: String, searchPath: String, home: Path): Path? {
        val set = configured.trim().takeIf { it.isNotEmpty() }?.let(Path::of)
        val above = generateSequence(project.toAbsolutePath()) { it.parent }.flatMap {
            sequenceOf(it.resolve("compiler/build/$NAME"), it.resolve("build/$NAME"))
        }
        val searched = searchPath.split(':').filter { it.startsWith("/") }.map { Path.of(it, NAME) }
        val known = listOf(Path.of("/opt/homebrew/bin", NAME), Path.of("/usr/local/bin", NAME))
        // A Path is itself a list of its parts: it is wrapped, or `+` would add "home", ".local", "bin".
        val last = sequenceOf(home.resolve(".local/bin/$NAME"))
        val candidates = sequenceOf(set).filterNotNull() + above + searched + known + last
        return candidates.firstOrNull { Files.isExecutable(it) && !Files.isDirectory(it) }
    }
}
