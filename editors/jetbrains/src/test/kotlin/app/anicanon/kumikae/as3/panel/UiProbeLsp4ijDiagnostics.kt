package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.language.As3Lsp4ijClient
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.psi.PsiDocumentManager
import com.redhat.devtools.lsp4ij.DocumentContentSynchronizer
import com.redhat.devtools.lsp4ij.LanguageServerManager
import org.eclipse.lsp4j.Diagnostic
import org.eclipse.lsp4j.DiagnosticSeverity
import org.eclipse.lsp4j.Position
import org.eclipse.lsp4j.PublishDiagnosticsParams
import org.eclipse.lsp4j.Range
import java.util.concurrent.TimeUnit

/** Checks publication decisions with the real client/cache and an observable rendering boundary. */
internal object UiProbeLsp4ijDiagnostics {
    fun run(project: Project, editor: Editor, observations: MutableList<String>) {
        val wrapper = checkNotNull(
            LanguageServerManager.getInstance(project).getLanguageServer("kumikae.as3").get(10, TimeUnit.SECONDS),
        ).serverWrapper
        val file = checkNotNull(FileDocumentManager.getInstance().getFile(editor.document))
        val uri = wrapper.toUri(file)
        val opened = checkNotNull(wrapper.getOpenedDocument(uri, false))
        val synchronizer = checkNotNull(opened.synchronizer)
        val versionMethod = DocumentContentSynchronizer::class.java.getDeclaredMethod("getVersion")
        check(versionMethod.trySetAccessible())
        val diagnostic =
            Diagnostic(Range(Position(0, 0), Position(0, 7)), "publication probe", DiagnosticSeverity.Error, "probe")
        var refreshes = 0
        val client = As3Lsp4ijClient(project) { target, document ->
            check(target == file && document === editor.document)
            refreshes++
        }
        try {
            client.setServerWrapper(wrapper)
            client.connect(checkNotNull(wrapper.languageServer))
            UiProbeCompletion.edt {
                WriteCommandAction.runWriteCommandAction(project) {
                    // The native handler uses this monitor too. Keep real server replies out of this sequence.
                    synchronized(opened) {
                        val server = opened.diagnostics.toSet()
                        check(server.isNotEmpty()) { "The server's own diagnostic is not showing" }
                        try {
                            val version = versionMethod.invoke(synchronizer) as Int
                            val accepted = PublishDiagnosticsParams(uri.toString(), listOf(diagnostic), version)
                            client.publishDiagnostics(accepted)
                            check(refreshes == 1 && opened.diagnostics.contains(diagnostic)) {
                                "Current diagnostic was not accepted"
                            }
                            client.publishDiagnostics(
                                PublishDiagnosticsParams(uri.toString(), emptyList(), version - 1),
                            )
                            check(refreshes == 1 && opened.diagnostics.contains(diagnostic)) {
                                "Rejected publication requested a repaint"
                            }
                            client.publishDiagnostics(accepted)
                            check(refreshes == 2) { "Identical accepted diagnostic did not request a repaint" }
                            val source = editor.document.text
                            val psi = PsiDocumentManager.getInstance(project)
                            try {
                                editor.document.insertString(0, " ")
                                check(!psi.isCommitted(editor.document)) { "Expected an unsent edit" }
                                client.publishDiagnostics(
                                    PublishDiagnosticsParams(uri.toString(), emptyList(), version),
                                )
                                check(refreshes == 2 && opened.diagnostics.contains(diagnostic)) {
                                    "Publication for old text was accepted during an uncommitted edit"
                                }
                            } finally {
                                editor.document.setText(source)
                                psi.commitDocument(editor.document)
                            }
                            client.dispose()
                            val latest = versionMethod.invoke(synchronizer) as Int
                            client.publishDiagnostics(PublishDiagnosticsParams(uri.toString(), emptyList(), latest))
                            check(refreshes == 2 && opened.diagnostics.contains(diagnostic)) {
                                "Disposed client changed diagnostics"
                            }
                        } finally {
                            // Undo only the probe's publications, inside the monitor: no server reply lands
                            // between, and the server's own diagnostics are what it left.
                            val latest = versionMethod.invoke(synchronizer) as Int
                            wrapper.updateDiagnostics(file, "lsp4ij.publish", server.toList(), latest)
                        }
                        check(opened.diagnostics.toSet() == server) { "Cleanup changed the server's own diagnostics" }
                    }
                }
            }
            observations +=
                "Stale, uncommitted and disposed replies are ignored; equal accepted diagnostics request repaint"
        } finally {
            if (!client.isDisposed) client.dispose()
        }
    }
}
