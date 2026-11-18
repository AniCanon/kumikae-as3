package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.testing.TestSource
import com.intellij.icons.AllIcons
import com.intellij.ide.projectView.PresentationData
import com.intellij.ide.projectView.ProjectViewNode
import com.intellij.ide.projectView.ViewSettings
import com.intellij.ide.projectView.impl.nodes.PsiFileNode
import com.intellij.ide.util.treeView.AbstractTreeNode
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiFile
import javax.swing.Icon

internal class As3MemberFileNode(project: Project, file: PsiFile, settings: ViewSettings) :
    PsiFileNode(project, file, settings) {
    override fun getChildrenImpl(): Collection<AbstractTreeNode<*>> {
        val file = value ?: return emptyList()
        val game = project ?: return emptyList()
        val parsed = TestSource.read(file.text)
        if (parsed.classOffset < 0) return emptyList()
        fun member(name: String, offset: Int, icon: Icon) = DeclarationNode(
            game,
            file.virtualFile,
            settings,
            name,
            offset,
            icon,
        )
        val fields = parsed.fields.map { member(it.name, it.offset, AllIcons.Nodes.Field) }
        val methods = parsed.methods.map {
            member(
                it.name,
                it.offset,
                if (it.annotated) AllIcons.RunConfigurations.TestState.Run else AllIcons.Nodes.Method,
            )
        }
        return listOf(
            DeclarationNode(
                game,
                file.virtualFile,
                settings,
                parsed.className,
                parsed.classOffset,
                if (parsed.isInterface) AllIcons.Nodes.Interface else AllIcons.Nodes.Class,
                fields + methods,
            ),
        )
    }
}

private class DeclarationNode(
    project: Project,
    private val file: VirtualFile,
    settings: ViewSettings,
    private val label: String,
    private val offset: Int,
    private val icon: Icon,
    private val members: List<AbstractTreeNode<*>> = emptyList(),
) : ProjectViewNode<Pair<VirtualFile, Int>>(project, file to offset, settings) {
    override fun getChildren() = members
    override fun contains(file: VirtualFile) = this.file == file
    override fun update(data: PresentationData) {
        data.presentableText = label
        data.setIcon(icon)
    }
    override fun canNavigate() = file.isValid
    override fun canNavigateToSource() = canNavigate()
    override fun navigate(requestFocus: Boolean) {
        project?.let { OpenFileDescriptor(it, file, offset).navigate(requestFocus) }
    }
}
