package app.anicanon.kumikae.as3.scene

import com.intellij.codeInsight.daemon.LineMarkerInfo
import com.intellij.codeInsight.daemon.LineMarkerProvider
import com.intellij.codeInsight.navigation.NavigationGutterIconBuilder
import com.intellij.icons.AllIcons
import com.intellij.openapi.actionSystem.ActionUpdateThread
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.popup.JBPopupFactory
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiManager
import com.intellij.psi.search.FilenameIndex
import com.intellij.psi.search.GlobalSearchScope
import com.intellij.util.concurrency.AppExecutorUtil

internal data class SceneUsage(val file: VirtualFile, val node: SceneNode) {
    override fun toString() = "${file.name} · ${node.path} (${node.type})"
}

internal fun sceneUsages(project: Project, script: VirtualFile): List<SceneUsage> {
    val root =
        generateSequence(script.parent) { it.parent }.firstOrNull { it.findChild("project.godot") != null }
            ?: return emptyList()
    val relative = VfsUtilCore.getRelativePath(script, root, '/') ?: return emptyList()
    return FilenameIndex.getAllFilesByExt(project, "tscn", GlobalSearchScope.projectScope(project)).flatMap { file ->
        if (!VfsUtilCore.isAncestor(root, file, true)) return@flatMap emptyList()
        val source =
            FileDocumentManager.getInstance().getDocument(file)?.immutableCharSequence?.toString()
                ?: return@flatMap emptyList()
        runCatching {
            SceneDocument(source).nodes.filter { it.script == "res://$relative" }.map { SceneUsage(file, it) }
        }
            .getOrDefault(emptyList())
    }
}

class FindAttachedScenesAction : AnAction("Find Attached Scene Nodes") {
    override fun getActionUpdateThread() = ActionUpdateThread.BGT
    override fun update(event: AnActionEvent) {
        event.presentation.isEnabledAndVisible = event.getData(CommonDataKeys.VIRTUAL_FILE)?.extension == "as"
    }
    override fun actionPerformed(event: AnActionEvent) {
        val project = event.project ?: return
        val script = event.getData(CommonDataKeys.VIRTUAL_FILE) ?: return
        ReadAction.nonBlocking<List<SceneUsage>> { sceneUsages(project, script) }.expireWith(project)
            .finishOnUiThread(ModalityState.defaultModalityState()) { usages ->
                JBPopupFactory.getInstance().createPopupChooserBuilder(usages)
                    .setTitle(if (usages.isEmpty()) "No attached scene nodes" else "Attached scene nodes")
                    .setItemChosenCallback { OpenFileDescriptor(project, it.file, it.node.offset).navigate(true) }
                    .createPopup().showInBestPositionFor(event.dataContext)
            }.submit(AppExecutorUtil.getAppExecutorService())
    }
}

class AttachedScenesLineMarker : LineMarkerProvider {
    override fun getLineMarkerInfo(element: PsiElement): LineMarkerInfo<*>? {
        if (element.firstChild != null || element.text != "class") return null
        val script = element.containingFile?.virtualFile ?: return null
        if (script.extension != "as") return null
        val targets = sceneUsages(element.project, script).mapNotNull {
            PsiManager.getInstance(element.project).findFile(it.file)
        }.distinct()
        if (targets.isEmpty()) return null
        return NavigationGutterIconBuilder.create(AllIcons.Nodes.Folder).setTargets(targets)
            .setTooltipText("Open a scene that attaches this AS3 script").createLineMarkerInfo(element)
    }
}
