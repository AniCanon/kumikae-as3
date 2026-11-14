package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.run.GodotLaunch
import com.intellij.ide.FileIconProvider
import com.intellij.ide.projectView.TreeStructureProvider
import com.intellij.ide.projectView.ViewSettings
import com.intellij.ide.projectView.impl.nodes.PsiDirectoryNode
import com.intellij.ide.projectView.impl.nodes.PsiFileNode
import com.intellij.ide.util.treeView.AbstractTreeNode
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.IconLoader
import com.intellij.openapi.util.Key
import com.intellij.openapi.vfs.VirtualFile
import java.nio.file.Path
import javax.swing.Icon

/** What a person writing a game does not open: what Godot and the IDE keep for themselves. */
internal object Clutter {
    private val folders = setOf(".godot", ".idea", "bin")
    private val endings = listOf(".uid", ".import", ".gdextension")

    fun isClutter(name: String, isFolder: Boolean): Boolean =
        if (isFolder) name in folders else endings.any(name::endsWith)
}

/** A script's own icon, in the project view and on editor tabs. */
class As3FileIcon : FileIconProvider {
    override fun getIcon(file: VirtualFile, flags: Int, project: Project?): Icon? =
        if (!file.isDirectory && file.extension == "as") ICON else null

    private companion object {
        val ICON: Icon = IconLoader.getIcon("/icons/as3File.svg", As3FileIcon::class.java)
    }
}

/** In a project that is an AS3 Godot game, the project view shows the game: scripts, scenes, settings. */
class GameProjectView : TreeStructureProvider {
    override fun modify(
        parent: AbstractTreeNode<*>,
        children: MutableCollection<AbstractTreeNode<*>>,
        settings: ViewSettings?,
    ): MutableCollection<AbstractTreeNode<*>> {
        val project = parent.project ?: return children
        if (!isGame(project)) return children
        return children.filterNotTo(ArrayList()) { node ->
            when (node) {
                is PsiDirectoryNode -> node.virtualFile?.let { Clutter.isClutter(it.name, isFolder = true) } == true
                is PsiFileNode -> node.virtualFile?.let { Clutter.isClutter(it.name, isFolder = false) } == true
                else -> false
            }
        }
    }

    private fun isGame(project: Project): Boolean {
        project.getUserData(GAME)?.let { return it }
        val game =
            project.basePath?.let { runCatching { GodotLaunch.isAs3Game(Path.of(it)) }.getOrDefault(false) } ?: false
        project.putUserData(GAME, game)
        return game
    }

    private companion object {
        val GAME = Key.create<Boolean>("kumikae.as3.isGame")
    }
}
