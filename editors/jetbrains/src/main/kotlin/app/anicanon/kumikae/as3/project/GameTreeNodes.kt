package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.settings.GameArtwork
import app.anicanon.kumikae.as3.settings.GodotConfig
import com.intellij.icons.AllIcons
import com.intellij.ide.projectView.PresentationData
import com.intellij.ide.projectView.ProjectViewNode
import com.intellij.ide.projectView.ViewSettings
import com.intellij.ide.projectView.impl.nodes.PsiDirectoryNode
import com.intellij.ide.projectView.impl.nodes.PsiFileNode
import com.intellij.ide.util.treeView.AbstractTreeNode
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiFile
import javax.swing.Icon

internal class GameRootNode(private val game: Project, node: PsiDirectoryNode) : PsiDirectoryNode(node) {
    private val projectFile get() = virtualFile?.findChild("project.godot")
    override fun update(data: PresentationData) {
        super.update(data)
        val config = projectFile?.let { FileDocumentManager.getInstance().getDocument(it) } ?: return
        val identity = runCatching { GodotConfig(config.text) }.getOrNull() ?: return
        val gameName = identity.string("application", "config/name")
        if (gameName.isNotBlank()) data.presentableText = gameName
        val image = virtualFile?.let {
            GameArtwork.preview(it, identity.string("application", "config/icon"))
        }
        data.setIcon(image?.treeIcon ?: PluginFiles.mark)
    }
    override fun canNavigate() = projectFile != null
    override fun navigate(requestFocus: Boolean) {
        projectFile?.let { FileEditorManager.getInstance(game).openFile(it, requestFocus) }
    }
}

internal class GameConfigNode(
    project: Project,
    file: PsiFile,
    settings: ViewSettings,
    private val label: String,
    private val icon: Icon,
) : PsiFileNode(project, file, settings) {
    override fun update(data: PresentationData) {
        super.update(data)
        data.presentableText = label
        data.locationString = value?.name
        data.setIcon(icon)
    }
}

internal class GameScenesGroup(
    project: Project,
    settings: ViewSettings,
    private val scenes: List<AbstractTreeNode<*>>,
) : ProjectViewNode<String>(project, "Scenes", settings) {
    override fun getChildren(): Collection<AbstractTreeNode<*>> = scenes
    override fun contains(file: VirtualFile) = scenes.any { (it as? ProjectViewNode<*>)?.contains(file) == true }
    override fun update(data: PresentationData) {
        data.presentableText = "Scenes"
        data.setIcon(AllIcons.Nodes.Folder)
    }
}

internal fun organizeGameRoot(
    project: Project,
    settings: ViewSettings,
    nodes: MutableCollection<AbstractTreeNode<*>>,
): MutableCollection<AbstractTreeNode<*>> {
    val result = arrayListOf<AbstractTreeNode<*>>()
    val scenes = arrayListOf<AbstractTreeNode<*>>()
    for (node in nodes) {
        val file = (node as? PsiFileNode)?.value
        when {
            file?.virtualFile?.extension in setOf("tscn", "scn") -> scenes += node

            file?.name in setOf(".gitignore", "kas3-tests.json") && !GameViewOptions.generated(project) -> Unit

            file?.name == "project.godot" ->
                result +=
                    GameConfigNode(project, file, settings, "Godot Project", AllIcons.General.Settings)

            file?.name == "export_presets.cfg" ->
                result +=
                    GameConfigNode(project, file, settings, "Targets", AllIcons.RunConfigurations.Application)

            else -> result += node
        }
    }
    if (scenes.isNotEmpty()) result += GameScenesGroup(project, settings, scenes)
    return result
}
