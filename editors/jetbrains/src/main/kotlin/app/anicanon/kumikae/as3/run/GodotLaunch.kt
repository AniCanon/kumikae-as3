package app.anicanon.kumikae.as3.run

import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes

/** What running a Godot game needs to know, worked out without the IDE. */
internal object GodotLaunch {
    private const val SCRIPT = ".as"
    private const val DEPTH = 6

    /** Godot's program: the one that was set, one named `godot` on the search path, or the usual application. */
    fun candidates(
        preferred: List<String>,
        searchPath: String,
        applications: List<Path>,
        installed: List<Path> = listOf(Path.of("/opt/homebrew/bin"), Path.of("/usr/local/bin")),
    ): List<Path> {
        val configured = preferred.map(String::trim).filter(String::isNotEmpty).map(Path::of)
        // An IDE started from the Dock has a short search path: where Godot is usually installed is looked in too.
        val folders = searchPath.split(':').filter { it.startsWith("/") }.map(Path::of) + installed
        val named = folders.flatMap { listOf(it.resolve("godot"), it.resolve("godot4")) }
        val apps = applications.flatMap { folder ->
            listOf("Godot.app", "Godot_mono.app").map { folder.resolve("$it/Contents/MacOS/Godot") }
        }
        return (configured + named + apps).distinct().filter {
            Files.isRegularFile(it) && Files.isExecutable(it)
        }
    }

    fun command(godot: Path, project: Path, scene: String): List<String> =
        listOf(godot.toString(), "--path", project.toString()) + listOfNotNull(scene.trim().takeIf { it.isNotEmpty() })

    /** A folder with a Godot project file and at least one AS3 script, not counting what Godot keeps for itself. */
    fun isAs3Game(project: Path): Boolean {
        if (!Files.isRegularFile(project.resolve("project.godot"))) return false
        if (Files.isRegularFile(project.resolve("kas3.gdextension"))) return true
        var found = false
        try {
            Files.walkFileTree(
                project,
                emptySet(),
                DEPTH,
                object : SimpleFileVisitor<Path>() {
                    override fun preVisitDirectory(dir: Path, attrs: BasicFileAttributes): FileVisitResult =
                        if (dir != project && dir.fileName.toString().startsWith(".")) {
                            FileVisitResult.SKIP_SUBTREE
                        } else {
                            FileVisitResult.CONTINUE
                        }

                    override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                        if (attrs.isRegularFile && file.fileName.toString().endsWith(SCRIPT)) {
                            found = true
                            return FileVisitResult.TERMINATE
                        }
                        return FileVisitResult.CONTINUE
                    }

                    override fun visitFileFailed(file: Path, exc: IOException) = FileVisitResult.CONTINUE
                },
            )
        } catch (_: IOException) {
            return false
        }
        return found
    }
}
