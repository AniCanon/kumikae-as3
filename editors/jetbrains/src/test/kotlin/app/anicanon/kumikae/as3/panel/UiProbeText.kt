package app.anicanon.kumikae.as3.panel

import javax.swing.text.JTextComponent
import javax.swing.text.Utilities

/** Checks on rendered text: what a person reads is wrapped between words. */
internal object UiProbeText {
    /** Rows of a wrapped text that end inside a word although the row had a space to break at. */
    fun brokenWords(text: JTextComponent): List<String> {
        val content = text.document.getText(0, text.document.length)
        val broken = mutableListOf<String>()
        var start = 0
        while (start < content.length) {
            val end = Utilities.getRowEnd(text, start)
            check(end >= start) { "No row at $start of the wrapped text" }
            val next = end + 1
            val row = content.substring(start, next)
            if (next < content.length && !content[end].isWhitespace() && !content[next].isWhitespace() &&
                row.trimEnd().contains(' ')
            ) {
                broken += "${row.trim()}|${content.substring(next).substringBefore('\n').take(WORD)}"
            }
            start = next
        }
        return broken
    }

    private const val WORD = 12
}
