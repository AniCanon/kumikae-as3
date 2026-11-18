package app.anicanon.kumikae.as3.testing

import app.anicanon.kumikae.as3.language.As3Lexer
import app.anicanon.kumikae.as3.language.As3Tokens
import com.intellij.psi.TokenType

internal data class TestField(val name: String, val offset: Int, val constant: Boolean)

internal data class TestMethod(val name: String, val offset: Int, val end: Int, val annotated: Boolean = false)

/** Declaration locations only. The manifest remains the authority for runnable cases. */
internal data class TestSource(
    val className: String,
    val classOffset: Int,
    val methods: List<TestMethod>,
    val packageName: String = "",
    val suite: Boolean = false,
    val fields: List<TestField> = emptyList(),
    val isInterface: Boolean = false,
    val suiteFixtures: Set<String> = emptySet(),
) {
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
            val declaration = tokens.indexOfFirst { it.text in setOf("class", "interface") }
            if (declaration < 0) return TestSource("", -1, emptyList())
            val className = tokens.getOrNull(declaration + 1)?.text.orEmpty()
            var index = declaration + 2
            while (index < tokens.size && tokens[index].text != "{") index++
            index++
            var depth = 1
            val methods = mutableListOf<TestMethod>()
            val fields = mutableListOf<TestField>()
            val fieldTypes = mutableListOf<String>()
            while (index < tokens.size && depth > 0) {
                val token = tokens[index]
                if (depth == 1 && token.text in setOf("var", "const")) {
                    tokens.getOrNull(index + 1)?.let { fields += TestField(it.text, it.start, token.text == "const") }
                    if (tokens.getOrNull(index + 2)?.text == ":") {
                        fieldTypes += tokens.drop(index + 3).takeWhile { it.text !in setOf(";", "=", ",", "}") }
                            .joinToString("") { it.text }
                    }
                }
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
                    val annotated =
                        index >= 4 &&
                            tokens.subList(index - 4, index).map { it.text } == listOf("[", "Test", "]", "public")
                    methods += TestMethod(name, token.start, tokens.getOrNull(end - 1)?.end ?: text.length, annotated)
                    index = end
                    continue
                }
                if (token.text == "{") depth++
                if (token.text == "}") depth--
                index++
            }
            val pkg = tokens.indexOfFirst { it.text == "package" }
            val prefix = if (pkg <
                0
            ) {
                ""
            } else {
                tokens.drop(pkg + 1).takeWhile { it.text != "{" }.joinToString("") { it.text }
            }
            val suite = tokens.take(declaration).map { it.text }.windowed(3).any { it == listOf("[", "Suite", "]") }
            val imports = tokens.take(declaration).mapIndexedNotNull { at, token ->
                if (token.text ==
                    "import"
                ) {
                    tokens.drop(at + 1).takeWhile { it.text != ";" }.joinToString("") { it.text }
                } else {
                    null
                }
            }
            val fixtures = if (!suite) {
                emptySet()
            } else {
                fieldTypes.map { name ->
                    if ('.' in name) {
                        name
                    } else {
                        imports.find { it.substringAfterLast('.') == name }
                            ?: listOf(prefix, name).filter(String::isNotBlank).joinToString(".")
                    }
                }.toSet()
            }
            return TestSource(
                className,
                tokens[declaration].start,
                methods,
                prefix,
                suite,
                fields,
                tokens[declaration].text == "interface",
                fixtures,
            )
        }
    }
}
