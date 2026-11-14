package app.anicanon.kumikae.as3.language

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals

class As3LexerTest {
    private fun tokens(text: String): List<String> {
        val lexer = As3Lexer()
        lexer.start(text, 0, text.length, 0)
        val out = mutableListOf<String>()
        var covered = 0
        while (lexer.tokenType != null) {
            assertEquals(covered, lexer.tokenStart, "tokens leave no gap")
            out += "${lexer.tokenType}:${text.substring(lexer.tokenStart, lexer.tokenEnd)}"
            covered = lexer.tokenEnd
            lexer.advance()
        }
        assertEquals(text.length, covered, "tokens reach the end")
        return out.filterNot { it.startsWith("WHITE_SPACE") }
    }

    @Test fun `a name is one token, so that the place under the pointer is the whole name`() {
        assertEquals(
            listOf("AS3_WORD:queueRedraw", "AS3_LPAREN:(", "AS3_RPAREN:)", "AS3_SYMBOL:;"),
            tokens("queueRedraw();"),
        )
        assertEquals(
            listOf("AS3_WORD:_speed2", "AS3_SYMBOL:+", "AS3_SYMBOL:=", "AS3_WORD:\$x"),
            tokens("_speed2 += \$x"),
        )
    }

    @Test fun `comments and strings are each one token, whatever is in them`() {
        assertEquals(listOf("AS3_LINE_COMMENT:// a { b", "AS3_WORD:x"), tokens("// a { b\nx"))
        assertEquals(listOf("AS3_BLOCK_COMMENT:/* a\n b */", "AS3_WORD:x"), tokens("/* a\n b */ x"))
        assertEquals(listOf("AS3_STRING:\"a \\\" } b\"", "AS3_SYMBOL:;"), tokens("\"a \\\" } b\";"))
        assertEquals(listOf("AS3_STRING:'it'", "AS3_SYMBOL:;"), tokens("'it';"))
    }

    @Test fun `what is left open at the end of the file ends there`() {
        assertEquals(listOf("AS3_BLOCK_COMMENT:/* never closed"), tokens("/* never closed"))
        assertEquals(listOf("AS3_STRING:\"never closed"), tokens("\"never closed"))
        assertEquals(
            listOf("AS3_WORD:a", "AS3_STRING:\"stops at the line's end", "AS3_WORD:b"),
            tokens("a \"stops at the line's end\nb"),
        )
    }

    @Test fun `brackets are told apart, for matching`() {
        assertEquals(
            listOf("AS3_LBRACE:{", "AS3_LBRACKET:[", "AS3_LPAREN:(", "AS3_RPAREN:)", "AS3_RBRACKET:]", "AS3_RBRACE:}"),
            tokens("{[()]}"),
        )
    }

    @Test fun `an empty file has no tokens`() {
        assertEquals(emptyList(), tokens(""))
    }
}
