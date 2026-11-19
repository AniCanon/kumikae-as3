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
    private val folders = setOf(".godot", ".idea", ".run", "bin", ".build", "build")
    private val endings = listOf(".uid", ".import", ".gdextension")

    fun hideFolder(name: String, projectRoot: Boolean): Boolean = projectRoot && isClutter(name, isFolder = true)

    /**
     * The folders at a game's root that hold builds, not the game: a Simulator build stages a copy of the
     * project in `.build`. The IDE does not index them, so search and navigation never offer a copy of a
     * script, in which nothing is completed. `.idea` is the IDE's own, and `.godot` holds what Rider's Godot
     * support registers: of it, only `.godot/kas3` is left out, where Play keeps the game as C++, its
     * libraries and their logs, written again at each save.
     */
    fun unindexed(root: String): List<String> =
        (folders - setOf(".idea", ".godot", ".run")).map { "$root/$it" } + "$root/$PLAY_BUILDS"

    private const val PLAY_BUILDS = ".godot/kas3"

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
        if (settings == null) return visible
        if (parent is GameScenesGroup) return visible
        val nodes = visible.mapTo(ArrayList()) { node ->
            val psiFile = (node as? PsiFileNode)?.value
            if (node is PsiFileNode && node !is As3MemberFileNode && node.virtualFile?.extension == "as" &&
                psiFile != null && GameViewOptions.members(project)
            ) {
                As3MemberFileNode(project, psiFile, settings)
            } else if (node !is PsiDirectoryNode) {
                node
            } else {
                if (node.virtualFile?.path == project.basePath) return@mapTo GameRootNode(project, node)
                gameFolder(node, project, settings)
                    ?: if (GameViewOptions.compact(project)) packageNode(node, project, settings) else node
            }
        }
        return if ((parent as? PsiDirectoryNode)?.virtualFile?.path == project.basePath) {
            organizeGameRoot(project, settings, nodes)
        } else {
            nodes
        }
    }

    private fun isGame(project: Project): Boolean {
        if (project.getUserData(GAME) == true) return true
        val game =
            project.basePath?.let { runCatching { GodotLaunch.isAs3Game(Path.of(it)) }.getOrDefault(false) } ?: false
        if (game) project.putUserData(GAME, true)
        return game
    }

    private companion object {
        val GAME = Key.create<Boolean>("kumikae.as3.isGame")
    }
}
