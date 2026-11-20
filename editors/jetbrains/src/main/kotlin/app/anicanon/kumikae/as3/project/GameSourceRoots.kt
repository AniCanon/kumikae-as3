package app.anicanon.kumikae.as3.project

import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.application.WriteAction
import com.intellij.openapi.module.Module
import com.intellij.openapi.module.ModuleManager
import com.intellij.openapi.module.ModuleType
import com.intellij.openapi.project.Project
import com.intellij.openapi.roots.ModuleRootModificationUtil
import com.intellij.openapi.roots.ProjectFileIndex
import com.intellij.openapi.vfs.LocalFileSystem
import java.nio.file.Path

/** Rider folder projects can have no module. Keep IDE metadata under .idea alongside Rider's own. */
internal object GameSourceRoots {
    fun register(project: Project, base: Path) {
        val root = LocalFileSystem.getInstance().findFileByNioFile(base) ?: return
        val folders = listOf("src", "tests").mapNotNull { root.findChild(it)?.takeIf { child -> child.isDirectory } }
        if (folders.isEmpty()) return
        val index = ProjectFileIndex.getInstance(project)
        val modules = ModuleManager.getInstance(project)
        val module = ReadAction.computeBlocking<Module?, RuntimeException> { index.getModuleForFile(root) } ?: run {
            if (modules.modules.isNotEmpty()) return
            WriteAction.computeAndWait<Module, RuntimeException> {
                modules.newModule(base.resolve(".idea/KumikaeGame.iml"), ModuleType.EMPTY.id)
            }
        }
        ModuleRootModificationUtil.modifyModel(module) { model ->
            val entry = model.contentEntries.firstOrNull { it.file == root } ?: if (model.contentEntries.isEmpty()) {
                model.addContentEntry(root)
            } else {
                return@modifyModel false
            }
            val missing = folders.filter { folder -> entry.sourceFolders.none { it.url == folder.url } }
            for (folder in missing) entry.addSourceFolder(folder, folder.name == "tests")
            missing.isNotEmpty()
        }
    }
}
