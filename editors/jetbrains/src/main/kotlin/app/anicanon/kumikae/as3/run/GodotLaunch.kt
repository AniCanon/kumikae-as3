package app.anicanon.kumikae.as3.run

import java.nio.file.Files
import java.nio.file.Path

/** What running a Godot game needs to know, worked out without the IDE. */
internal object GodotLaunch {
    private const val SCRIPT = ".as"
    private const val DEPTH = 6

    /** Godot's program: the one that was set, one named `godot` on the search path, or the usual application. */
    fun findGodot(
        configured: String,
        searchPath: String,
        applications: Path = Path.of("/Applications"),
        installed: List<Path> = listOf(Path.of("/opt/homebrew/bin"), Path.of("/usr/local/bin")),
    ): Path? {
        val set = configured.trim().takeIf { it.isNotEmpty() }?.let(Path::of)
        // An IDE started from the Dock has a short search path: where Godot is usually installed is looked in too.
        val folders = searchPath.split(':').filter { it.startsWith("/") }.map(Path::of) + installed
        val named = folders.flatMap { listOf(it.resolve("godot"), it.resolve("godot4")) }
        val apps = listOf("Godot.app", "Godot_mono.app").map { applications.resolve("$it/Contents/MacOS/Godot") }
        return (listOfNotNull(set) + named + apps).firstOrNull { Files.isExecutable(it) && !Files.isDirectory(it) }
    }

    fun command(godot: Path, project: Path, scene: String): List<String> =
        listOf(godot.toString(), "--path", project.toString()) + listOfNotNull(scene.trim().takeIf { it.isNotEmpty() })

    /** A folder with a Godot project file and at least one AS3 script, not counting what Godot keeps for itself. */
    fun isAs3Game(project: Path): Boolean {
        if (!Files.isRegularFile(project.resolve("project.godot"))) return false
        if (Files.isRegularFile(project.resolve("kas3.gdextension"))) return true
        return Files.walk(project, DEPTH).use { found ->
            found.filter { path -> project.relativize(path).none { it.toString().startsWith(".") } }
                .anyMatch { it.toString().endsWith(SCRIPT) && Files.isRegularFile(it) }
        }
    }
}
