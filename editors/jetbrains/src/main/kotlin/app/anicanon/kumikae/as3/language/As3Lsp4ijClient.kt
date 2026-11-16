package app.anicanon.kumikae.as3.language

import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.editor.Document
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiDocumentManager
import com.redhat.devtools.lsp4ij.DocumentContentSynchronizer
import com.redhat.devtools.lsp4ij.client.LanguageClientImpl
import com.redhat.devtools.lsp4ij.client.features.FileUriSupport
import org.eclipse.lsp4j.PublishDiagnosticsParams

/** Keeps accepted diagnostics visible even when an edit invalidated an identical earlier repaint. */
internal class As3Lsp4ijClient(project: Project, private val refresh: (VirtualFile, Document) -> Unit) :
    LanguageClientImpl(project) {
    override fun publishDiagnostics(params: PublishDiagnosticsParams) {
        // Keep the client's version check and refresh stamp on the same document snapshot.
        ReadAction.runBlocking<RuntimeException> {
            if (isDisposed) return@runBlocking
            val file = FileUriSupport.findFileByUri(params.uri, clientFeatures) ?: return@runBlocking
            val uri = FileUriSupport.getFileUri(file, clientFeatures) ?: return@runBlocking
            val synchronizer = clientFeatures.serverWrapper.getOpenedDocument(uri, false)?.synchronizer
            // LSP4IJ sends edits on PSI commit; its version still names the old text before then.
            if (synchronizer != null && !PsiDocumentManager.getInstance(project).isCommitted(synchronizer.document)) {
                return@runBlocking
            }
            super.publishDiagnostics(params)
            if (synchronizer == null || !FileEditorManager.getInstance(project).isFileOpen(file)) return@runBlocking
            if (params.version != null && params.version != currentVersion.invoke(synchronizer)) return@runBlocking
            // Render the client's accepted cache, never the incoming payload. Native refreshes coalesce.
            refresh(file, synchronizer.document)
        }
    }

    private companion object {
        // LSP4IJ 0.21 exposes the synchronizer but keeps its version accessor package-private.
        val currentVersion = DocumentContentSynchronizer::class.java.getDeclaredMethod("getVersion").apply {
            check(returnType == Int::class.javaPrimitiveType && trySetAccessible()) {
                "LSP4IJ document-version access is unavailable"
            }
        }
    }
}
