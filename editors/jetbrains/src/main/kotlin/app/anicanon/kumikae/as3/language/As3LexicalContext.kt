package app.anicanon.kumikae.as3.language

/** Structural context only: names and types remain the compiler's responsibility. */
internal class As3LexicalContext {
    private val frames = ArrayDeque<Int>()
    var regexAllowed = true
        private set
    private var statement = true
    private var pending = NONE
    private var property = false

    // Common nesting fits in a portable restart state. Deeper input uses a prefix replay on restart,
    // never a truncated stack or an interning table that grows with the document's edit history.
    val state: Int
        get() {
            if (frames.size > MAX_PACKED_DEPTH) return REPLAY
            var stack = 1
            for (frame in frames) stack = (stack shl 4) or frame
            return (stack shl FLAG_BITS) or (pending shl 3) or
                (if (property) 4 else 0) or (if (statement) 2 else 0) or (if (regexAllowed) 1 else 0)
        }

    fun restore(state: Int) {
        frames.clear()
        regexAllowed = state == 0 || state and 1 != 0
        statement = state == 0 || state and 2 != 0
        property = state and 4 != 0
        pending = (state ushr 3) and 7
        var stack = state ushr FLAG_BITS
        while (stack > 1) {
            frames.addFirst(stack and 15)
            stack = stack ushr 4
        }
    }

    fun value() {
        regexAllowed = false
        statement = false
        property = false
    }

    fun word(word: String) {
        if (property) {
            value()
            return
        }
        when {
            word == "function" -> pending = if (statement) FUNCTION_DECLARATION else FUNCTION_EXPRESSION
            word in CONTROL -> pending = CONTROL_PAREN
            word == "each" && pending == CONTROL_PAREN -> Unit
            word == "class" || word == "interface" || word == "package" -> pending = DECLARATION_BODY
        }
        regexAllowed = word in PREFIXES
        statement = word in STATEMENTS || (statement && word in MODIFIERS)
    }

    fun punctuation(char: Char) {
        when (char) {
            '(' -> {
                frames.addLast(
                    when (pending) {
                        CONTROL_PAREN -> CONTROL_PAREN
                        FUNCTION_DECLARATION -> FUNCTION_DECLARATION
                        FUNCTION_EXPRESSION -> FUNCTION_EXPRESSION
                        else -> PAREN
                    },
                )
                pending = NONE
                statement = false
                regexAllowed = true
            }

            ')' -> {
                val frame = pop()
                pending = when (frame) {
                    FUNCTION_DECLARATION -> DECLARATION_BODY
                    FUNCTION_EXPRESSION -> EXPRESSION_BODY
                    else -> NONE
                }
                statement = frame == CONTROL_PAREN
                regexAllowed = statement
            }

            '{' -> {
                val frame = when {
                    pending == EXPRESSION_BODY -> EXPRESSION_BODY
                    pending == DECLARATION_BODY || statement -> BLOCK
                    else -> OBJECT
                }
                frames.addLast(frame)
                pending = NONE
                statement = frame != OBJECT
                regexAllowed = true
            }

            '}' -> {
                statement = pop() == BLOCK
                regexAllowed = statement
                pending = NONE
            }

            '?' -> {
                frames.addLast(CONDITIONAL)
                statement = false
                regexAllowed = true
            }

            ':' -> {
                statement = when (frames.lastOrNull()) {
                    CONDITIONAL -> {
                        pop()
                        false
                    }

                    OBJECT -> {
                        pending = NONE
                        false
                    }

                    else -> pending == NONE
                }
                regexAllowed = true
            }

            ';' -> {
                statement = true
                regexAllowed = true
                pending = NONE
            }

            else -> {
                regexAllowed = char !in "]."
                statement = false
            }
        }
        property = char == '.'
    }

    private fun pop() = if (frames.isEmpty()) NONE else frames.removeLast()

    companion object {
        const val REPLAY = -1
        private const val FLAG_BITS = 6
        private const val MAX_PACKED_DEPTH = 6
        private const val CONDITIONAL = 8
        private const val NONE = 0
        private const val BLOCK = 1
        private const val OBJECT = 2
        private const val PAREN = 3
        private const val CONTROL_PAREN = 4
        private const val FUNCTION_DECLARATION = 5
        private const val FUNCTION_EXPRESSION = 6
        private const val EXPRESSION_BODY = 7
        private const val DECLARATION_BODY = BLOCK
        private val CONTROL = setOf("if", "while", "for", "switch", "catch", "with")
        private val PREFIXES =
            setOf("else", "do", "return", "throw", "case", "delete", "typeof", "void", "new", "in", "is", "as")
        private val STATEMENTS = setOf("else", "do", "try", "finally")
        private val MODIFIERS =
            setOf("public", "private", "protected", "internal", "static", "final", "override", "native", "dynamic")
    }
}
