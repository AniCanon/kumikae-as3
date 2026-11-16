package app.anicanon.kumikae.as3.project

import com.intellij.icons.AllIcons
import com.intellij.ide.projectView.PresentationData
import com.intellij.ide.projectView.ViewSettings
import com.intellij.ide.projectView.impl.nodes.PsiDirectoryNode
import com.intellij.openapi.project.Project
import com.intellij.psi.PsiDirectory

internal class PackageDirectoryNode(
    project: Project,
    directory: PsiDirectory,
    settings: ViewSettings,
    private val label: String,
) : PsiDirectoryNode(project, directory, settings) {
    override fun update(data: PresentationData) {
        super.update(data)
        data.presentableText = label
        data.setIcon(AllIcons.Nodes.Package)
    }
}

internal fun packageNode(node: PsiDirectoryNode, project: Project, settings: ViewSettings): PsiDirectoryNode {
    var directory = node.value ?: return node
    var ancestor = directory.parentDirectory
    var source = false
    while (ancestor != null && ancestor.virtualFile.path != project.basePath) {
        if (ancestor.name in setOf("src", "tests")) source = true
        ancestor = ancestor.parentDirectory
    }
    if (!source) return node
    val chain = mutableListOf(directory)
    while (directory.files.isEmpty() && directory.subdirectories.size == 1) {
        directory = directory.subdirectories.single()
        chain += directory
    }
    var folder = PackageFolder(chain.last().name, directory.files.size)
    for (item in chain.dropLast(1).asReversed()) folder = PackageFolder(item.name, 0, listOf(folder))
    return PackageDirectoryNode(project, directory, settings, compactPackage(folder).joinToString("."))
}
