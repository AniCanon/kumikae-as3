package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.run.GodotLaunch
import com.intellij.ide.projectView.ProjectView
import com.intellij.ide.projectView.TreeStructureProvider
import com.intellij.ide.projectView.ViewSettings
import com.intellij.ide.projectView.impl.nodes.PsiDirectoryNode
import com.intellij.ide.projectView.impl.nodes.PsiFileNode
import com.intellij.ide.util.treeView.AbstractTreeNode
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Key
import java.nio.file.Path

/** What a person writing a game does not open: what Godot and the IDE keep for themselves. */
internal object Clutter {
    private val folders = setOf(".godot", ".idea", "bin", ".build", "build")
    private val endings = listOf(".uid", ".import", ".gdextension")

    fun hideFolder(name: String, projectRoot: Boolean): Boolean = projectRoot && isClutter(name, isFolder = true)

    fun isClutter(name: String, isFolder: Boolean): Boolean =
        if (isFolder) name in folders else endings.any(name::endsWith)
}

/** In a project that is an AS3 Godot game, the project view shows the game: scripts, scenes, settings. */
class GameProjectView : TreeStructureProvider {
    override fun modify(
        parent: AbstractTreeNode<*>,
        children: MutableCollection<AbstractTreeNode<*>>,
        settings: ViewSettings?,
    ): MutableCollection<AbstractTreeNode<*>> {
        val project = parent.project ?: return children
        // Only in the Game view: the IDE's other views show everything, as they always have.
        if (ProjectView.getInstance(project).currentViewId != GamePane.ID || !isGame(project)) return children
        val visible = if (GameViewOptions.generated(project)) {
            children
        } else {
            children.filterNotTo(ArrayList()) { node ->
                when (node) {
                    is PsiDirectoryNode -> node.virtualFile?.let {
                        Clutter.hideFolder(
                            it.name,
                            projectRoot =
                            it.parent?.path == project.basePath,
                        )
                    } ==
                        true

                    is PsiFileNode -> node.virtualFile?.let { Clutter.isClutter(it.name, isFolder = false) } == true

                    else -> false
                }
            }
        }
        if (!GameViewOptions.compact(project) || settings == null) return visible
        return visible.mapTo(ArrayList()) { node ->
            if (node is PsiDirectoryNode) packageNode(node, project, settings) else node
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
