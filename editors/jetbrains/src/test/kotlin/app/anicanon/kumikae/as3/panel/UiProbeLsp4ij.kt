package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.Project
import com.intellij.psi.PsiDocumentManager
import com.redhat.devtools.lsp4ij.LanguageServerManager
import com.redhat.devtools.lsp4ij.ServerStatus
import com.redhat.devtools.lsp4ij.client.ExecuteLSPFeatureStatus
import com.redhat.devtools.lsp4ij.client.indexing.ProjectIndexingManager

internal object UiProbeLsp4ij {
    fun canComplete(project: Project, editor: Editor): Boolean {
        val file = PsiDocumentManager.getInstance(project).getPsiFile(editor.document) ?: return false
        return ProjectIndexingManager.canExecuteLSPFeature(file) == ExecuteLSPFeatureStatus.NOW
    }

    fun ready(project: Project): Boolean = status(project) == ServerStatus.started
    private fun status(project: Project): ServerStatus? =
        LanguageServerManager.getInstance(project).getServerStatus("kumikae.as3")
}
