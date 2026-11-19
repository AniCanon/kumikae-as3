package app.anicanon.kumikae.as3.settings

import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardOpenOption.CREATE_NEW
import java.nio.file.StandardOpenOption.WRITE

internal object ProjectImageImport {
    /** CREATE_NEW also protects simultaneous imports and existing symlink entries. */
    fun copy(source: Path, root: Path): String {
        require(Files.isRegularFile(source)) { "Choose an image file to import." }
        val project = root.toRealPath()
        val folder = Files.createDirectories(project.resolve("assets")).toRealPath()
        require(folder.startsWith(project) && folder != project) { "The assets directory must be inside this project." }
        val resourceFolder = project.relativize(folder).joinToString("/")
        require(
            generateSequence(folder) { it.parent }.takeWhile { it != project }.none {
                it.fileName.toString().startsWith('.') || Files.exists(it.resolve(".gdignore")) ||
                    Files.exists(it.resolve("project.godot"))
            },
        ) { "The assets directory must be visible to Godot." }
        val name = requireNotNull(source.fileName?.toString()) { "Choose an image file to import." }
        require(
            name.isNotEmpty() && !name.startsWith('.') && !name.contains('\\') &&
                !name.contains(':') && name.none(Char::isISOControl),
        ) { "Choose a visible image with a Godot resource name." }
        val dot = name.lastIndexOf('.')
        val stem = if (dot > 0) name.substring(0, dot) else name
        val suffix = if (dot > 0) name.substring(dot) else ""
        var index = 1
        Files.newInputStream(source).use { input ->
            while (true) {
                val target = folder.resolve(if (index == 1) name else "$stem-$index$suffix")
                val output = try {
                    Files.newOutputStream(target, CREATE_NEW, WRITE)
                } catch (_: FileAlreadyExistsException) {
                    index++
                    continue
                }
                try {
                    output.use { input.copyTo(it) }
                } catch (failure: Exception) {
                    runCatching { Files.deleteIfExists(target) }.onFailure(failure::addSuppressed)
                    throw failure
                }
                return "res://$resourceFolder/${target.fileName}"
            }
        }
    }
}
