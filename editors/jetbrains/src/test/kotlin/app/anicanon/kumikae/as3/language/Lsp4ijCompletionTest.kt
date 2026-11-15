package app.anicanon.kumikae.as3.language

import org.eclipse.lsp4j.ClientCapabilities
import org.eclipse.lsp4j.InitializeParams
import org.eclipse.lsp4j.InsertReplaceEdit
import org.eclipse.lsp4j.Position
import org.eclipse.lsp4j.PublishDiagnosticsCapabilities
import org.eclipse.lsp4j.Range
import org.eclipse.lsp4j.TextDocumentClientCapabilities
import org.eclipse.lsp4j.TextEdit
import org.eclipse.lsp4j.jsonrpc.messages.Either
import org.junit.jupiter.api.Test
import kotlin.test.assertEquals

class Lsp4ijCompletionTest {
    @Test fun `client advertises its diagnostic version filtering`() {
        val diagnostics = PublishDiagnosticsCapabilities()
        val params = InitializeParams().apply {
            capabilities = ClientCapabilities().apply {
                textDocument = TextDocumentClientCapabilities().apply { publishDiagnostics = diagnostics }
            }
        }
        As3Lsp4ijFactory().createClientFeatures().initializeParams(params)
        assertEquals(true, diagnostics.versionSupport)
    }

    @Test fun `Enter inserts at the request caret while Tab replaces the suffix`() {
        val range = Range(Position(2, 5), Position(2, 15))
        val edit = Either.forLeft<TextEdit, InsertReplaceEdit>(TextEdit(range, "normalized()"))
        assertEquals(Range(Position(2, 5), Position(2, 9)), selectCompletionEdit(edit, Position(2, 9), false).range)
        assertEquals(range, selectCompletionEdit(edit, Position(2, 9), true).range)
        assertEquals(Position(2, 15), edit.left.range.end)
    }

    @Test fun `client adjustment cannot mutate server positions or the captured caret`() {
        val caret = Position(0, 4)
        val range = Range(Position(0, 1), Position(0, 8))
        for (replace in listOf(false, true)) {
            val item = Either.forLeft<TextEdit, InsertReplaceEdit>(TextEdit(range, "value"))
            val selected = selectCompletionEdit(item, caret, replace)
            selected.range.start.character = 0
            selected.range.end.character += 3
            assertEquals(Position(0, 1), range.start)
            assertEquals(Position(0, 8), range.end)
            assertEquals(Position(0, 4), caret)
        }
    }

    @Test fun `explicit insert and replace ranges are respected`() {
        val insert = Range(Position(0, 1), Position(0, 3))
        val replace = Range(Position(0, 1), Position(0, 8))
        val edit = Either.forRight<TextEdit, InsertReplaceEdit>(InsertReplaceEdit("value", insert, replace))
        assertEquals(insert, selectCompletionEdit(edit, Position(0, 3), false).range)
        assertEquals(replace, selectCompletionEdit(edit, Position(0, 3), true).range)
    }

    @Test fun `multiline and edits outside the caret keep their server range`() {
        for (range in listOf(Range(Position(0, 0), Position(1, 4)), Range(Position(1, 1), Position(1, 2)))) {
            val edit = Either.forLeft<TextEdit, InsertReplaceEdit>(TextEdit(range, "value"))
            assertEquals(range, selectCompletionEdit(edit, Position(1, 3), false).range)
        }
    }
}
