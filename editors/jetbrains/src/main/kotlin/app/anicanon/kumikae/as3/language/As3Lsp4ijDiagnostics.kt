package app.anicanon.kumikae.as3.language

import com.intellij.codeInsight.intention.IntentionAction
import com.intellij.lang.annotation.AnnotationHolder
import com.intellij.openapi.editor.Document
import com.intellij.openapi.util.TextRange
import com.redhat.devtools.lsp4ij.client.features.LSPDiagnosticFeature
import org.eclipse.lsp4j.Diagnostic
import org.eclipse.lsp4j.Position
import org.eclipse.lsp4j.Range

/** LSP4IJ may render an accepted publication after a subsequent document edit. */
internal class As3Lsp4ijDiagnostics : LSPDiagnosticFeature() {
    override fun createAnnotation(
        diagnostic: Diagnostic,
        document: Document,
        fixes: List<IntentionAction>,
        holder: AnnotationHolder,
    ) {
        // The collector holds read access. Do not clamp stale positions onto unrelated source text.
        if (!diagnosticRangeFits(diagnostic.range, document)) return
        if (document.textLength != 0) {
            super.createAnnotation(diagnostic, document, fixes, holder)
            return
        }
        // DocumentImpl has no lines when empty; LSP4IJ's line-based converter cannot handle the origin.
        val severity = getHighlightSeverity(diagnostic) ?: return
        val annotation = holder.newAnnotation(severity, getMessage(diagnostic))
            .tooltip(getTooltip(diagnostic)).range(TextRange.EMPTY_RANGE).afterEndOfLine()
        getProblemHighlightType(diagnostic)?.let { annotation.highlightType(it) }
        fixes.forEach { annotation.withFix(it) }
        annotation.create()
    }
}

internal fun diagnosticRangeFits(range: Range, document: Document): Boolean {
    if (document.textLength == 0) {
        return range.start.line == 0 && range.start.character == 0 && range.end.line == 0 && range.end.character == 0
    }
    fun fits(position: Position): Boolean {
        if (position.line !in 0 until document.lineCount || position.character < 0) return false
        val length = document.getLineEndOffset(position.line) - document.getLineStartOffset(position.line)
        return position.character <= length
    }
    val start = range.start
    val end = range.end
    return fits(start) && fits(end) &&
        (start.line < end.line || (start.line == end.line && start.character <= end.character))
}
