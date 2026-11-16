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
            val restart = As3Lexer()
            restart.start(text, lexer.tokenStart, text.length, lexer.state)
            assertEquals(lexer.tokenType, restart.tokenType, "restart keeps token kind")
            assertEquals(lexer.tokenEnd, restart.tokenEnd, "restart keeps token boundary")
            lexer.advance()
            restart.advance()
            assertEquals(lexer.tokenType, restart.tokenType, "restart keeps next token")
            assertEquals(lexer.state, restart.state, "restart keeps next state")
            assertEquals(lexer.tokenEnd, restart.tokenEnd, "restart keeps next boundary")
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

    @Test fun `regex punctuation stays inside one token while division remains code`() {
        assertEquals(listOf("AS3_REGEX:/[(),.]/"), tokens("/[(),.]/"))
        assertEquals(listOf("AS3_REGEX:/a\\/b/"), tokens("/a\\/b/"))
        assertEquals(listOf("AS3_REGEX:/unfinished"), tokens("/unfinished"))
        assertEquals(listOf("AS3_WORD:x", "AS3_SYMBOL:/", "AS3_WORD:y"), tokens("x / y"))
    }

    @Test fun `regex after a control condition stays a literal`() {
        for (source in listOf("if (ready) /[()]/", "while (ready()) /[()]/", "for (;;) /[()]/")) {
            assertEquals("AS3_REGEX:/[()]/", tokens(source).last())
        }
        assertEquals("AS3_WORD:y", tokens("call() / y").last())
    }

    @Test fun `an empty file has no tokens`() {
        assertEquals(emptyList(), tokens(""))
    }

    @Test fun `closing a statement block permits regex but closing a value permits division`() {
        for (source in listOf(
            "if (ready) {} /[()]/",
            "{ if (ready) {} } /[()]/",
            "function run():void {} /[()]/",
            "class Main { public function run():void {} } /[()]/",
        )) {
            assertEquals("AS3_REGEX:/[()]/", tokens(source).last(), source)
        }
        for (source in listOf(
            "var value = {} / divisor",
            "var fn = function():Number {} / divisor",
            "var value = { nested: {} } / divisor",
            "call(function():void {}) / divisor",
        )) {
            assertEquals("AS3_WORD:divisor", tokens(source).last(), source)
        }
    }

    @Test fun `deep brace context survives lexer restarts and changed prefixes`() {
        val nested = "{".repeat(32) + "}".repeat(32) + " /[()]/"
        assertEquals("AS3_REGEX:/[()]/", tokens(nested).last())
        for (prefix in listOf("if (ready)", "var object =", "if (other())", "var other =")) {
            val suffix = if (prefix.startsWith("if")) " /[()]/" else " / divisor"
            val expected = if (prefix.startsWith("if")) "AS3_REGEX:/[()]/" else "AS3_WORD:divisor"
            assertEquals(expected, tokens("$prefix {}$suffix").last())
        }
    }

    @Test fun `object keys and conditional colons do not introduce statement blocks`() {
        for (keyword in listOf("if", "while", "function", "class")) {
            val actual = tokens("var object:Object = { $keyword: call() / divisor }")
            assertEquals(true, actual.contains("AS3_WORD:divisor"))
        }
        assertEquals("AS3_WORD:divisor", tokens("var value = ready ? 1 : {} / divisor").last())
        assertEquals("AS3_REGEX:/literal/", tokens("label: {} /literal/").last())
    }
}
