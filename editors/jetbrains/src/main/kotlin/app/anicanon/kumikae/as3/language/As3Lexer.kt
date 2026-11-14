package app.anicanon.kumikae.as3.language

import com.intellij.lexer.LexerBase
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType

/** The pieces of an AS3 file the IDE needs to tell apart. The compiler does the real reading. */
internal object As3Tokens {
    val WORD = IElementType("AS3_WORD", As3Language)
    val SYMBOL = IElementType("AS3_SYMBOL", As3Language)
    val STRING = IElementType("AS3_STRING", As3Language)
    val LINE_COMMENT = IElementType("AS3_LINE_COMMENT", As3Language)
    val BLOCK_COMMENT = IElementType("AS3_BLOCK_COMMENT", As3Language)
    val LBRACE = IElementType("AS3_LBRACE", As3Language)
    val RBRACE = IElementType("AS3_RBRACE", As3Language)
    val LPAREN = IElementType("AS3_LPAREN", As3Language)
    val RPAREN = IElementType("AS3_RPAREN", As3Language)
    val LBRACKET = IElementType("AS3_LBRACKET", As3Language)
    val RBRACKET = IElementType("AS3_RBRACKET", As3Language)
}

/**
 * Splits a file into names, strings, comments, brackets and everything else. It is what makes a name
 * one thing the IDE can ask about (go to declaration, hover) and lets brackets be matched; meaning comes
 * from `kas3c`, through the language server.
 */
internal class As3Lexer : LexerBase() {
    private var text: CharSequence = ""
    private var end = 0
    private var start = 0
    private var stop = 0
    private var type: IElementType? = null

    override fun start(buffer: CharSequence, startOffset: Int, endOffset: Int, initialState: Int) {
        text = buffer
        end = endOffset
        stop = startOffset
        advance()
    }

    override fun getState() = 0

    override fun getTokenType() = type

    override fun getTokenStart() = start

    override fun getTokenEnd() = stop

    override fun getBufferSequence() = text

    override fun getBufferEnd() = end

    override fun advance() {
        start = stop
        if (start >= end) {
            type = null
            return
        }
        val first = text[start]
        val next = if (start + 1 < end) text[start + 1] else ' '
        when {
            first.isWhitespace() -> take(TokenType.WHITE_SPACE) { it.isWhitespace() }

            isWord(first) -> take(As3Tokens.WORD, ::isWord)

            first == '/' && next == '/' -> take(As3Tokens.LINE_COMMENT) { it != '\n' }

            first == '/' && next == '*' -> blockComment()

            first == '"' || first == '\'' -> string(first)

            else -> {
                type = BRACKETS[first] ?: As3Tokens.SYMBOL
                stop = start + 1
            }
        }
    }

    private fun isWord(char: Char) = char.isLetterOrDigit() || char == '_' || char == '$'

    private fun take(found: IElementType, more: (Char) -> Boolean) {
        type = found
        stop = start + 1
        while (stop < end && more(text[stop])) stop++
    }

    private fun blockComment() {
        type = As3Tokens.BLOCK_COMMENT
        val close = text.indexOf("*/", start + 2)
        stop = if (close < 0 || close + 2 > end) end else close + 2
    }

    // A string ends at its quote, or at the end of its line: one left open while typing must not
    // swallow the rest of the file.
    private fun string(quote: Char) {
        type = As3Tokens.STRING
        stop = start + 1
        while (stop < end && text[stop] != '\n') {
            val here = text[stop++]
            if (here == '\\' && stop < end && text[stop] != '\n') {
                stop++
            } else if (here == quote) {
                return
            }
        }
    }

    private companion object {
        val BRACKETS = mapOf(
            '{' to As3Tokens.LBRACE,
            '}' to As3Tokens.RBRACE,
            '(' to As3Tokens.LPAREN,
            ')' to As3Tokens.RPAREN,
            '[' to As3Tokens.LBRACKET,
            ']' to As3Tokens.RBRACKET,
        )
    }
}
