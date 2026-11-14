package app.anicanon.kumikae.as3.language

import com.intellij.codeInsight.navigation.actions.GotoDeclarationHandler
import com.intellij.openapi.editor.Document
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.Project
import com.intellij.platform.lsp.api.LspClient
import com.intellij.platform.lsp.api.LspClientManager
import com.intellij.platform.lsp.api.LspServerState
import com.intellij.psi.PsiDocumentManager
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiManager
import org.eclipse.lsp4j.DefinitionParams
import org.eclipse.lsp4j.Location
import org.eclipse.lsp4j.Position

/**
 * Go to declaration for a script: `kas3c` is asked where the name under the caret is declared.
 *
 * The IDE's language-server support has its own way of answering this, but in Rider it is never asked
 * (the recorded exchange between the two shows no request for a definition), so the question is put to
 * the server from here, where the IDE does ask.
 */
class As3GotoDeclaration : GotoDeclarationHandler {
    override fun getGotoDeclarationTargets(source: PsiElement?, offset: Int, editor: Editor): Array<PsiElement>? {
        val file = source?.containingFile?.takeIf { it.language == As3Language } ?: return null
        val script = file.virtualFile ?: return null
        val project = file.project
        val client = LspClientManager.getInstance(project).getClients(As3LspSupportProvider::class.java)
            .firstOrNull { it.state == LspServerState.Running } ?: return null
        val asked = DefinitionParams(client.getDocumentIdentifier(script), position(editor.document, offset))
        val answer = runCatching {
            client.sendRequestSync(TIMEOUT_MILLIS) { it.textDocumentService.definition(asked) }
        }.getOrNull() ?: return null
        val places = if (answer.isLeft) {
            answer.left
        } else {
            answer.right.map {
                Location(it.targetUri, it.targetSelectionRange)
            }
        }
        return places.mapNotNull { target(project, client, it) }.toTypedArray().takeIf { it.isNotEmpty() }
    }

    // The protocol counts a line's characters as the document does: in UTF-16 units.
    private fun position(document: Document, offset: Int): Position {
        val line = document.getLineNumber(offset)
        return Position(line, offset - document.getLineStartOffset(line))
    }

    private fun target(project: Project, client: LspClient, place: Location): PsiElement? {
        val found = client.descriptor.findFileByUri(place.uri) ?: return null
        val file = PsiManager.getInstance(project).findFile(found) ?: return null
        val document = PsiDocumentManager.getInstance(project).getDocument(file) ?: return file
        val line = place.range.start.line.coerceIn(0, maxOf(0, document.lineCount - 1))
        val offset = (document.getLineStartOffset(line) + place.range.start.character).coerceIn(0, document.textLength)
        return file.findElementAt(offset) ?: file
    }

    private companion object {
        const val TIMEOUT_MILLIS = 3000
    }
}
