package app.anicanon.kumikae.as3.language

import com.intellij.lexer.LexerBase
import com.intellij.lexer.RestartableLexer
import com.intellij.lexer.TokenIterator
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType

/** The pieces of an AS3 file the IDE needs to tell apart. The compiler does the real reading. */
internal object As3Tokens {
    val WORD = IElementType("AS3_WORD", As3Language)
    val SYMBOL = IElementType("AS3_SYMBOL", As3Language)
    val REGEX = IElementType("AS3_REGEX", As3Language)
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
 *
 * Its state at a token is all it needs to start again there, so the editor may start it again at any token
 * near an edit. It fits in fifteen bits: the editor keeps sixteen of a restartable lexer's state, and only
 * a lexer that is restartable has its state kept and compared after an edit at all.
 */
internal class As3Lexer :
    LexerBase(),
    RestartableLexer {
    private var text: CharSequence = ""
    private var end = 0
    private var start = 0
    private var stop = 0
    private var type: IElementType? = null
    private var regexAllowed = true
    private var tokenState = 0
    private var conditionDepth = 0
    private var controlPending = false

    // What the open braces are, so that after a `}` a slash is read as the brace's closing says: a block
    // ends a statement (a regex may follow), a value (an object, a function written in place) is divided.
    private var braces = Braces()

    // A `{` here would open a value: after `=`, `(`, `,`, `return` and the like.
    private var valueNext = false

    // `function` was written where a value goes: its body's brace is a value.
    private var functionValue = false

    // A `?` waits for its `:`, after which a brace is a value.
    private var ternary = false

    // A type is being written after a `:` (or a `case` or a label is ending): `*`, `.<` and `>` are part
    // of it, and a brace after it begins a block.
    private var inType = false

    override fun start(buffer: CharSequence, startOffset: Int, endOffset: Int, initialState: Int) {
        text = buffer
        end = endOffset
        stop = startOffset
        regexAllowed = initialState and 1 == 0
        controlPending = initialState and 2 != 0
        valueNext = initialState and 4 != 0
        functionValue = initialState and 8 != 0
        ternary = initialState and 16 != 0
        inType = initialState and IN_TYPE != 0
        conditionDepth = (initialState ushr 5) and DEPTH_MASK
        braces = Braces.of(initialState ushr BRACES_SHIFT)
        advance()
    }

    override fun getState() = tokenState

    override fun getStartState() = 0

    override fun isRestartableState(state: Int) = true

    override fun start(
        buffer: CharSequence,
        startOffset: Int,
        endOffset: Int,
        initialState: Int,
        tokenIterator: TokenIterator?,
    ) = start(buffer, startOffset, endOffset, initialState)

    override fun getTokenType() = type

    override fun getTokenStart() = start

    override fun getTokenEnd() = stop

    override fun getBufferSequence() = text

    override fun getBufferEnd() = end

    override fun advance() {
        start = stop
        tokenState = flag(inType, IN_TYPE) or (braces.bits shl BRACES_SHIFT) or (conditionDepth shl 5) or
            flag(ternary, 16) or flag(functionValue, 8) or flag(valueNext, 4) or flag(controlPending, 2) or
            (if (regexAllowed) 0 else 1)
        if (start >= end) {
            type = null
            return
        }
        val first = text[start]
        val next = if (start + 1 < end) text[start + 1] else ' '
        when {
            first.isWhitespace() -> take(TokenType.WHITE_SPACE) { it.isWhitespace() }

            isWord(first) -> word()

            first == '/' && next == '/' -> take(As3Tokens.LINE_COMMENT) { it != '\n' }

            first == '/' && next == '*' -> blockComment()

            first == '"' || first == '\'' -> {
                string(first)
                regexAllowed = false
                valueNext = false
                inType = false
            }

            first == '/' && regexAllowed -> {
                regex()
                valueNext = false
                inType = false
            }

            (first == '+' || first == '-') && next == first -> {
                type = As3Tokens.SYMBOL
                stop = start + 2
                valueNext = false
                inType = false
            }

            else -> symbol(first)
        }
    }

    private fun word() {
        take(As3Tokens.WORD, ::isWord)
        val word = text.subSequence(start, stop).toString()
        regexAllowed = word in EXPRESSION_PREFIXES
        controlPending = word in CONTROL_PREFIXES || (controlPending && word == "each")
        if (word == "function") {
            functionValue = valueNext
        }
        valueNext = !inType && word in VALUE_PREFIXES
    }

    private fun symbol(first: Char) {
        type = BRACKETS[first] ?: As3Tokens.SYMBOL
        stop = start + 1
        regexAllowed = first !in ")]}."
        if (inType && first in TYPE_SYMBOLS) {
            return // Part of the type: `*`, `Vector.<int>`.
        }
        val typeStarts = first == ':' && !ternary && !braces.innermostIsValue
        inType = false
        when (first) {
            '{' -> {
                braces = braces.push(valueNext || functionValue)
                // A brace inside the parameters (a default value) leaves the body still to come.
                functionValue = functionValue && valueNext
            }

            '}' -> {
                regexAllowed = !braces.innermostIsValue
                braces = braces.pop()
            }

            '?' -> ternary = true

            '(' -> if (conditionDepth > 0) {
                conditionDepth = minOf(conditionDepth + 1, DEPTH_MASK)
            } else if (controlPending) {
                conditionDepth = 1
            }

            ')' -> if (conditionDepth > 0) {
                conditionDepth--
                regexAllowed = conditionDepth == 0
            }
        }
        valueNext = first in VALUE_SYMBOLS || (first == ':' && (ternary || braces.innermostIsValue))
        if (first == ':') {
            ternary = false
            inType = typeStarts
        }
        controlPending = false
    }

    private fun flag(set: Boolean, bit: Int) = if (set) bit else 0

    private fun regex() {
        type = As3Tokens.REGEX
        regexAllowed = false
        stop = start + 1
        var inClass = false
        while (stop < end && text[stop] != '\n' && text[stop] != '\r') {
            when (text[stop++]) {
                '\\' -> if (stop < end && text[stop] != '\n' && text[stop] != '\r') stop++

                '[' -> inClass = true

                ']' -> inClass = false

                '/' -> if (!inClass) {
                    while (stop < end && isWord(text[stop])) stop++
                    return
                }
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
        const val DEPTH_MASK = 7 // A control condition's parentheses, nested up to seven deep.
        const val IN_TYPE = 1 shl 15
        const val TYPE_SYMBOLS = "*.<>"
        const val BRACES_SHIFT = 8
        const val VALUE_SYMBOLS = "=([,?!~+-*/%&|^<>"
        val VALUE_PREFIXES = setOf("return", "throw", "typeof", "delete", "in", "new", "yield")
        val CONTROL_PREFIXES = setOf("if", "while", "for", "switch", "catch", "with")
        val EXPRESSION_PREFIXES =
            setOf("else", "do", "return", "throw", "case", "delete", "typeof", "void", "new", "in", "is", "as")
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
