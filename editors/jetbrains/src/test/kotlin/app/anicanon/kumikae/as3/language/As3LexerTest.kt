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

    @Test fun `after a closing brace, what the brace opened says whether a slash is a regex or division`() {
        // A block ends a statement: a regex may follow.
        val blocks =
            listOf("if (a) {} /re/", "function f():void {} /re/", "case 1: {} /re/", "{ {} /re/ }", "else {} /re/")
        for (source in blocks) {
            assertEquals(true, tokens(source).contains("AS3_REGEX:/re/"), source)
        }
        // A value ends in its brace: an object or a function written in place. A slash divides it.
        for (source in listOf(
            "var o = {} / 2",
            "x = function():void {} / 2",
            "a ? b : {} / 2",
            "f({ a: {} } / 2)",
            "return {} / 2",
            "x = { f: function() { if (a) {} } } / 2",
        )) {
            val found = tokens(source)
            assertEquals(listOf("AS3_SYMBOL:/", "AS3_WORD:2"), found.dropWhile { it != "AS3_SYMBOL:/" }.take(2), source)
            assertEquals(false, found.any { it.startsWith("AS3_REGEX") }, source)
        }
    }

    @Test fun `sixteen open braces are told apart, and deeper ones are counted`() {
        val deep = "{".repeat(15) + "x = {} / 2" + "}".repeat(15) + " /re/"
        assertEquals(listOf("AS3_SYMBOL:/", "AS3_WORD:2"), tokens(deep).dropWhile { it != "AS3_SYMBOL:/" }.take(2))
        assertEquals("AS3_REGEX:/re/", tokens(deep).last())
        // Past that a brace counts as a block; closing them all still leaves the file's top level.
        val deeper = "{".repeat(40) + "if (a) {} /x/" + "}".repeat(40) + " x = {} / 2"
        assertEquals("AS3_REGEX:/x/", tokens(deeper).first { it.startsWith("AS3_REGEX") })
        assertEquals(listOf("AS3_SYMBOL:/", "AS3_WORD:2"), tokens(deeper).takeLast(2))
    }

    @Test fun `an empty file has no tokens`() {
        assertEquals(emptyList(), tokens(""))
    }
}
