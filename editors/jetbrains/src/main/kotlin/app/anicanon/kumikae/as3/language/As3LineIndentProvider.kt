package app.anicanon.kumikae.as3.language

import com.intellij.application.options.CodeStyle
import com.intellij.lang.Language
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.ex.EditorEx
import com.intellij.openapi.project.Project
import com.intellij.psi.TokenType
import com.intellij.psi.codeStyle.lineIndent.LineIndentProvider

/** Enter indentation uses existing editor tokens and the project's tab/space settings. */
class As3LineIndentProvider : LineIndentProvider {
    override fun isSuitableFor(language: Language?) = language == As3Language

    override fun getLineIndent(project: Project, editor: Editor, language: Language?, offset: Int): String? {
        if (!isSuitableFor(language) || editor !is EditorEx) return null
        val document = editor.document
        if (offset <= 0 || offset > document.textLength) return ""
        val text = document.charsSequence
        val line = document.getLineNumber(offset)
        if (line == 0) return ""
        val previousStart = document.getLineStartOffset(line - 1)
        val previousEnd = document.getLineEndOffset(line - 1)
        var end = previousStart
        while (end < previousEnd && (text[end] == ' ' || text[end] == '\t')) end++
        val base = text.subSequence(previousStart, end).toString()
        val tokens = editor.highlighter.createIterator(document.getLineStartOffset(line) - 1)
        while (!tokens.atEnd() && (
                tokens.tokenType == TokenType.WHITE_SPACE ||
                    tokens.tokenType == As3Tokens.LINE_COMMENT || tokens.tokenType == As3Tokens.BLOCK_COMMENT
                )
        ) {
            tokens.retreat()
        }
        if (tokens.atEnd() || tokens.start < previousStart || tokens.tokenType != As3Tokens.LBRACE) return base
        val options = CodeStyle.getSettings(project).getIndentOptions(As3FileType.INSTANCE)
        val unit = if (options.USE_TAB_CHARACTER) "\t" else " ".repeat(options.INDENT_SIZE)
        return base + unit
    }
}
