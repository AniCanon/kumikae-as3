package app.anicanon.kumikae.as3.language

import com.intellij.lexer.Lexer
import com.intellij.openapi.editor.Document
import com.intellij.openapi.editor.colors.EditorColorsScheme
import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.editor.event.DocumentEvent
import com.intellij.openapi.editor.ex.util.LexerEditorHighlighter
import com.intellij.openapi.editor.highlighter.EditorHighlighter
import com.intellij.openapi.editor.highlighter.HighlighterClient
import com.intellij.openapi.editor.highlighter.HighlighterIterator
import com.intellij.openapi.fileTypes.EditorHighlighterProvider
import com.intellij.openapi.fileTypes.FileType
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.tree.IElementType
import org.jetbrains.plugins.textmate.language.syntax.highlighting.TextMateEditorHighlighterProvider

/** Native token boundaries for editing; the shared TextMate grammar still owns every color. */
class As3EditorHighlighterProvider : EditorHighlighterProvider {
    override fun getEditorHighlighter(
        project: Project?,
        fileType: FileType,
        virtualFile: VirtualFile?,
        colors: EditorColorsScheme,
    ): EditorHighlighter {
        val coloring = TextMateEditorHighlighterProvider().getEditorHighlighter(project, fileType, virtualFile, colors)
        return As3EditorHighlighter(coloring, LexerEditorHighlighter(As3StructuralHighlighter(), colors))
    }
}

private class As3StructuralHighlighter : SyntaxHighlighterBase() {
    override fun getHighlightingLexer(): Lexer = As3Lexer()

    override fun getTokenHighlights(tokenType: IElementType): Array<TextAttributesKey> = emptyArray()
}

private class As3EditorHighlighter(private val coloring: EditorHighlighter, private val structure: EditorHighlighter) :
    EditorHighlighter {
    override fun setEditor(editor: HighlighterClient) {
        coloring.setEditor(editor)
        structure.setEditor(editor)
    }

    override fun setText(text: CharSequence) {
        coloring.setText(text)
        structure.setText(text)
    }

    override fun setColorScheme(scheme: EditorColorsScheme) {
        coloring.setColorScheme(scheme)
        structure.setColorScheme(scheme)
    }

    override fun documentChanged(event: DocumentEvent) {
        coloring.documentChanged(event)
        structure.documentChanged(event)
    }

    override fun createIterator(startOffset: Int): HighlighterIterator =
        As3HighlightIterator(coloring.createIterator(startOffset), structure.createIterator(startOffset))
}

/** Intersects token intervals without copying text or retaining per-keystroke history. */
internal class As3HighlightIterator(
    private val coloring: HighlighterIterator,
    private val structure: HighlighterIterator,
) : HighlighterIterator {
    val structuralStart get() = structure.start
    val structuralEnd get() = structure.end

    override fun getStart() = maxOf(coloring.start, structure.start)

    override fun getEnd() = minOf(coloring.end, structure.end)

    override fun getTokenType(): IElementType = structure.tokenType

    override fun getTextAttributes() = coloring.textAttributes

    override fun getTextAttributesKeys(): Array<TextAttributesKey> = coloring.textAttributesKeys

    override fun getDocument(): Document = structure.document

    override fun atEnd() = structure.atEnd() || coloring.atEnd()

    override fun advance() {
        val end = end
        if (coloring.end == end) coloring.advance()
        if (structure.end == end) structure.advance()
    }

    override fun retreat() {
        if (atEnd()) {
            if (coloring.atEnd()) coloring.retreat()
            if (structure.atEnd()) structure.retreat()
        } else {
            val start = start
            if (coloring.start == start) coloring.retreat()
            if (structure.start == start) structure.retreat()
        }
    }
}
