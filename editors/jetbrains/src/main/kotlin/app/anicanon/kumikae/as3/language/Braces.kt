package app.anicanon.kumikae.as3.language

/**
 * Where the open braces that began a value (an object, a function written in place) are, among the others,
 * which began blocks. It is kept in seven bits of a lexer's state, because the editor keeps only sixteen
 * bits of it: for the innermost such brace, how many blocks are open inside it (up to 14), and for the one
 * around that, how many blocks lie between them (up to 6). Blocks need no more: any number may be open.
 * A value brace further out, or past those counts, is forgotten, and its `}` is read as a block's.
 */
@JvmInline
internal value class Braces private constructor(val bits: Int) {
    constructor() : this(0)

    // Each count is kept plus one, so that 0 means "none" and the empty state is 0.
    private val inner get() = bits and INNER_MASK
    private val outer get() = bits ushr INNER_BITS

    val innermostIsValue get() = inner == 1

    fun push(value: Boolean): Braces = when {
        value -> Braces(1 or (if (inner in 1..OUTER_MAX) inner shl INNER_BITS else 0))

        inner == 0 -> this

        inner == INNER_MASK -> Braces(0)

        // Too many blocks inside it to count: it is forgotten.
        else -> Braces((inner + 1) or (outer shl INNER_BITS))
    }

    fun pop(): Braces = when (inner) {
        0 -> this
        1 -> Braces(outer)
        else -> Braces((inner - 1) or (outer shl INNER_BITS))
    }

    companion object {
        const val BITS = 7
        private const val INNER_BITS = 4
        private const val INNER_MASK = (1 shl INNER_BITS) - 1
        private const val OUTER_MAX = (1 shl (BITS - INNER_BITS)) - 1

        fun of(bits: Int) = Braces(bits and ((1 shl BITS) - 1))
    }
}
