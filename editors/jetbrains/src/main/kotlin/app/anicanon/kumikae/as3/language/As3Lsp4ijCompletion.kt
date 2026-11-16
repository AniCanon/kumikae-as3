package app.anicanon.kumikae.as3.language

import com.intellij.codeInsight.lookup.Lookup
import com.intellij.codeInsight.lookup.LookupElement
import com.intellij.codeInsight.lookup.LookupEvent
import com.intellij.codeInsight.lookup.LookupListener
import com.intellij.codeInsight.lookup.LookupManagerListener
import com.intellij.openapi.util.Key
import com.intellij.psi.PsiFile
import com.redhat.devtools.lsp4ij.client.features.LSPCompletionFeature
import org.eclipse.lsp4j.CompletionItem
import org.eclipse.lsp4j.InsertReplaceEdit
import org.eclipse.lsp4j.Position
import org.eclipse.lsp4j.Range
import org.eclipse.lsp4j.TextEdit
import org.eclipse.lsp4j.jsonrpc.messages.Either

/** LSP4IJ applies ordinary text edits as replacements for both keys; AS3 keeps native Enter/Tab semantics. */
internal class As3Lsp4ijCompletion : LSPCompletionFeature() {
    // This enables LSP4IJ's prefix-change restart, including imports hidden at an empty prefix.
    override fun useContextAwareSorting(file: PsiFile): Boolean = true

    override fun createLookupElement(item: CompletionItem, context: LSPCompletionContext): LookupElement? {
        val document = context.parameters.editor.document
        val offset = context.parameters.offset
        val line = document.getLineNumber(offset)
        val caret = Position(line, offset - document.getLineStartOffset(line))
        val proposal = super.createLookupElement(item, context) ?: return null
        proposal.putUserData(PENDING_EDIT, PendingEdit(item, caret))
        return proposal
    }
}

private data class PendingEdit(val item: CompletionItem, val caret: Position)

private val PENDING_EDIT = Key.create<PendingEdit>("kumikae.as3.completionEdit")

/** Choose the server range before LSP4IJ inserts; the lookup owns the listener and item data. */
internal class As3Lsp4ijLookupListener : LookupManagerListener {
    override fun activeLookupChanged(oldLookup: Lookup?, newLookup: Lookup?) {
        newLookup?.addLookupListener(object : LookupListener {
            override fun beforeItemSelected(event: LookupEvent): Boolean {
                val pending = event.item?.getUserData(PENDING_EDIT) ?: return true
                pending.item.textEdit?.let {
                    pending.item.textEdit = Either.forLeft(
                        selectCompletionEdit(it, pending.caret, event.completionChar == Lookup.REPLACE_SELECT_CHAR),
                    )
                }
                return true
            }
        })
    }
}

internal fun selectCompletionEdit(
    edit: Either<TextEdit, InsertReplaceEdit>,
    caret: Position,
    replace: Boolean,
): TextEdit {
    if (edit.isRight) {
        val value = edit.right
        return copiedEdit(if (replace) value.replace else value.insert, value.newText)
    }
    val value = edit.left
    val range = value.range
    val insertEnd = !replace && range.start.line == caret.line && range.end.line == caret.line &&
        caret.character in range.start.character..range.end.character
    return copiedEdit(if (insertEnd) Range(range.start, caret) else range, value.newText)
}

// LSP4IJ adjusts positions in-place after lookup typing; server ranges and the captured caret stay immutable.
private fun copiedEdit(range: Range, text: String): TextEdit = TextEdit(
    Range(Position(range.start.line, range.start.character), Position(range.end.line, range.end.character)),
    text,
)
