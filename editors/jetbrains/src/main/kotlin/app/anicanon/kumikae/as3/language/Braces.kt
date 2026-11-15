package app.anicanon.kumikae.as3.language

/**
 * The kinds of the braces open at a place, innermost last, in the bits of a lexer's state: one bit for
 * each, above a leading 1 that marks where they start. Sixteen are told apart. Braces open deeper than
 * that are counted, so that closing them comes out right, and count as blocks.
 */
@JvmInline
internal value class Braces private constructor(val bits: Int) {
    constructor() : this(EMPTY)

    val innermostIsValue get() = deeper == 0 && kinds > EMPTY && kinds and 1 == 1

    private val kinds get() = bits and KINDS_MASK
    private val deeper get() = bits ushr KIND_BITS

    fun push(value: Boolean): Braces = when {
        deeper > 0 || kinds >= FULL -> Braces(kinds or (minOf(deeper + 1, DEEPER_MAX) shl KIND_BITS))
        else -> Braces((kinds shl 1) or (if (value) 1 else 0))
    }

    fun pop(): Braces = when {
        deeper > 0 -> Braces(kinds or ((deeper - 1) shl KIND_BITS))
        kinds > EMPTY -> Braces(kinds ushr 1)
        else -> this // A `}` with nothing open: the file is being typed.
    }

    companion object {
        private const val KIND_BITS = 17
        private const val KINDS_MASK = (1 shl KIND_BITS) - 1
        private const val EMPTY = 1
        private const val FULL = 1 shl (KIND_BITS - 1) // The leading 1 is at the top: no room for another.
        private const val DEEPER_MAX = 31

        /** From the state's bits; 0, a fresh start, has nothing open. */
        fun of(bits: Int) = if (bits and KINDS_MASK == 0) Braces() else Braces(bits)
    }
}
