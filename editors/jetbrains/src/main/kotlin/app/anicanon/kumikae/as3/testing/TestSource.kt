package app.anicanon.kumikae.as3.testing

import app.anicanon.kumikae.as3.language.As3Lexer
import app.anicanon.kumikae.as3.language.As3Tokens
import com.intellij.psi.TokenType

internal data class TestMethod(val name: String, val offset: Int, val end: Int)

/** Declaration locations only. The manifest remains the authority for runnable cases. */
internal data class TestSource(val className: String, val classOffset: Int, val methods: List<TestMethod>) {
    fun methodAt(offset: Int) = methods.firstOrNull { offset in it.offset until it.end }

    companion object {
        private data class Token(val text: String, val start: Int, val end: Int)
        fun read(text: String): TestSource {
            val lexer = As3Lexer()
            lexer.start(text)
            val tokens = mutableListOf<Token>()
            while (lexer.tokenType != null) {
                if (lexer.tokenType !in
                    listOf(TokenType.WHITE_SPACE, As3Tokens.LINE_COMMENT, As3Tokens.BLOCK_COMMENT)
                ) {
                    val value = if (lexer.tokenType in
                        listOf(As3Tokens.STRING, As3Tokens.REGEX)
                    ) {
                        ""
                    } else {
                        lexer.tokenText
                    }
                    tokens += Token(value, lexer.tokenStart, lexer.tokenEnd)
                }
                lexer.advance()
            }
            val declaration = tokens.indexOfFirst { it.text == "class" }
            if (declaration < 0) return TestSource("", -1, emptyList())
            val className = tokens.getOrNull(declaration + 1)?.text.orEmpty()
            var index = declaration + 2
            while (index < tokens.size && tokens[index].text != "{") index++
            index++
            var depth = 1
            val methods = mutableListOf<TestMethod>()
            while (index < tokens.size && depth > 0) {
                val token = tokens[index]
                val accessor = tokens.getOrNull(index + 1)?.text in listOf("get", "set") &&
                    tokens.getOrNull(index + 3)?.text == "("
                val parametersAt = index + if (accessor) 3 else 2
                if (depth == 1 && token.text == "function" && tokens.getOrNull(parametersAt)?.text == "(") {
                    val name = if (accessor) {
                        tokens[index + 1].text + " " + tokens[index + 2].text
                    } else {
                        tokens[index + 1].text
                    }
                    var end = parametersAt + 1
                    var parameters = 1
                    while (end < tokens.size && parameters > 0) {
                        if (tokens[end].text == "(") parameters++
                        if (tokens[end].text == ")") parameters--
                        end++
                    }
                    while (end < tokens.size && tokens[end].text !in listOf("{", ";")) end++
                    if (tokens.getOrNull(end)?.text == "{") {
                        var body = 1
                        end++
                        while (end < tokens.size && body > 0) {
                            if (tokens[end].text == "{") body++
                            if (tokens[end].text == "}") body--
                            end++
                        }
                    } else {
                        end++
                    }
                    methods += TestMethod(name, token.start, tokens.getOrNull(end - 1)?.end ?: text.length)
                    index = end
                    continue
                }
                if (token.text == "{") depth++
                if (token.text == "}") depth--
                index++
            }
            return TestSource(className, tokens[declaration].start, methods)
        }
    }
}
