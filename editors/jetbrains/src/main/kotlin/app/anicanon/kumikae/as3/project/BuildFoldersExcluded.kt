package app.anicanon.kumikae.as3.project

import com.intellij.openapi.project.Project
import com.intellij.openapi.roots.impl.DirectoryIndexExcludePolicy
import com.intellij.openapi.vfs.VfsUtilCore
import java.nio.file.Files
import java.nio.file.Path

/**
 * An AS3 game's build folders, and what Play builds under `.godot/kas3`, are not part of what the IDE indexes:
 * see [Clutter.unindexed]. A game is known by the extension's own file at its root, so no other project's `bin`
 * or `build` is touched.
 */
internal class BuildFoldersExcluded(private val project: Project) : DirectoryIndexExcludePolicy {
    override fun getExcludeUrlsForProject(): Array<String> {
        val root = project.basePath?.takeIf { Files.isRegularFile(Path.of(it, "kas3.gdextension")) }
            ?: return emptyArray()
        return Clutter.unindexed(root).map(VfsUtilCore::pathToUrl).toTypedArray()
    }
}
