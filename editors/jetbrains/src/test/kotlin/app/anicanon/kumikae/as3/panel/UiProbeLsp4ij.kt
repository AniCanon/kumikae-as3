package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.Project
import com.intellij.psi.PsiDocumentManager
import com.redhat.devtools.lsp4ij.LSPIJUtils
import com.redhat.devtools.lsp4ij.LanguageServerManager
import com.redhat.devtools.lsp4ij.LanguageServiceAccessor
import com.redhat.devtools.lsp4ij.ServerStatus
import com.redhat.devtools.lsp4ij.client.ExecuteLSPFeatureStatus
import com.redhat.devtools.lsp4ij.client.indexing.ProjectIndexingManager
import java.util.concurrent.TimeUnit

internal object UiProbeLsp4ij {
    fun canComplete(project: Project, editor: Editor): Boolean {
        val file = PsiDocumentManager.getInstance(project).getPsiFile(editor.document) ?: return false
        return ProjectIndexingManager.canExecuteLSPFeature(file) == ExecuteLSPFeatureStatus.NOW
    }

    /**
     * Sends the edits LSP4IJ still holds. It sends them from a callback once the document is committed, on
     * another path than requests, so a request made at once can reach the server before the edit it follows
     * (seen in a trace: completion at the new caret, then the didChange that typed the new text).
     */
    fun flush(project: Project, editor: Editor) {
        val uri = LSPIJUtils.toUri(editor.document)
        LanguageServiceAccessor.getInstance(project).startedServers
            .mapNotNull { it.getOpenedDocument(uri)?.synchronizer?.flushPendingChanges() }
            .forEach { it.get(FLUSH_SECONDS, TimeUnit.SECONDS) }
    }

    fun ready(project: Project): Boolean = status(project) == ServerStatus.started
    private fun status(project: Project): ServerStatus? =
        LanguageServerManager.getInstance(project).getServerStatus("kumikae.as3")

    private const val FLUSH_SECONDS = 5L
}
