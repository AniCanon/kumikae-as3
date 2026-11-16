package app.anicanon.kumikae.as3.language

import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.editor.Document
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiDocumentManager
import com.redhat.devtools.lsp4ij.client.LanguageClientImpl
import com.redhat.devtools.lsp4ij.client.features.FileUriSupport
import org.eclipse.lsp4j.PublishDiagnosticsParams

/** Keeps accepted diagnostics visible even when an edit invalidated an identical earlier repaint. */
internal class As3Lsp4ijClient(
    project: Project,
    private val versions: SentVersions,
    private val refresh: (VirtualFile, Document) -> Unit,
) : LanguageClientImpl(project) {
    override fun publishDiagnostics(params: PublishDiagnosticsParams) {
        // Keep the client's version check and the refresh on the same document snapshot.
        ReadAction.runBlocking<RuntimeException> {
            if (isDisposed) return@runBlocking
            val file = FileUriSupport.findFileByUri(params.uri, clientFeatures) ?: return@runBlocking
            val document = FileDocumentManager.getInstance().getCachedDocument(file)
            // LSP4IJ sends edits on PSI commit; its version still names the old text before then.
            if (document != null && !PsiDocumentManager.getInstance(project).isCommitted(document)) {
                return@runBlocking
            }
            super.publishDiagnostics(params)
            if (document == null || !FileEditorManager.getInstance(project).isFileOpen(file)) return@runBlocking
            // LSP4IJ took the publication only if it names the version last sent.
            if (!versions.current(params.uri, params.version)) return@runBlocking
            // Render the client's accepted cache, never the incoming payload. Native refreshes coalesce.
            refresh(file, document)
        }
    }
}
