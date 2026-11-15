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
            // As the editor gives a state back: the low sixteen bits, sign and all.
            restart.start(text, lexer.tokenStart, text.length, (lexer.state shl 16) shr 16)
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
            listOf(
                "if (a) {} /re/",
                "function f():void {} /re/",
                "function f():* {} /re/",
                "function f():Vector.<int> {} /re/",
                "case 1: {} /re/",
                "{ {} /re/ }",
                "else {} /re/",
            )
        for (source in blocks) {
            assertEquals(true, tokens(source).contains("AS3_REGEX:/re/"), source)
        }
        // A value ends in its brace: an object or a function written in place. A slash divides it.
        for (source in listOf(
            "var o = {} / 2",
            "x = function():void {} / 2",
            "x = function():* {} / 2",
            "var o:Object = {} / 2",
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

    @Test fun `blocks may be open to any depth, and the two innermost values are kept`() {
        val deep = "{".repeat(40) + "x = {} / 2; if (a) {} /re/" + "}".repeat(40) + " /re/"
        assertEquals(listOf("AS3_SYMBOL:/", "AS3_WORD:2"), tokens(deep).dropWhile { it != "AS3_SYMBOL:/" }.take(2))
        assertEquals(2, tokens(deep).count { it == "AS3_REGEX:/re/" })
        // An object in an object, each with blocks inside: both are told apart from the blocks.
        val nested = "x = { a: function() { if (b) { } }, c: { d: 1 } / 2 } / 3"
        assertEquals(listOf("AS3_SYMBOL:/", "AS3_SYMBOL:/"), tokens(nested).filter { it == "AS3_SYMBOL:/" })
        // A third value around them is forgotten: its `}` is read as a block's.
        assertEquals("AS3_REGEX:/ 2", tokens("x = { a: { b: { c: 1 } } } / 2").last())
    }

    @Test fun `the whole state fits in the sixteen bits the editor keeps`() {
        val text =
            "{".repeat(40) + "x = { a: function():Vector.<int> { if (((((((((b))))))))) {} } } / 2" + "}".repeat(40)
        val lexer = As3Lexer()
        lexer.start(text, 0, text.length, 0)
        while (lexer.tokenType != null) {
            assertEquals(lexer.state, lexer.state and 0xFFFF)
            lexer.advance()
        }
    }

    @Test fun `the editor may start again at any token, as tokens() checks for every one`() {
        // The state holds all the lexer knows: an edit is lexed again from a token near it, not from the
        // top of the file, though inside a class the state is never the first one.
        val lexer = As3Lexer()
        assertEquals(0, lexer.startState)
        val text = "package { class A { function f():void { x = {} / 2; } } }"
        lexer.start(text, 0, text.length, 0)
        while (lexer.tokenType != null) {
            assertEquals(true, lexer.isRestartableState(lexer.state))
            lexer.advance()
        }
    }

    @Test fun `an empty file has no tokens`() {
        assertEquals(emptyList(), tokens(""))
    }
}
