package app.anicanon.kumikae.as3.panel

import java.awt.Component
import java.awt.Container
import javax.swing.JLabel
import javax.swing.SwingUtilities
import javax.swing.text.JTextComponent
import javax.swing.text.Utilities

/** Checks on rendered text: what a person reads must be whole, and wrapped between words. */
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

    /** Whether the label or text that starts with [prefix] is drawn whole inside [root]. */
    fun whole(root: Component, prefix: String): Boolean {
        val shown = descendants(root).filter { it.isVisible && textOf(it)?.startsWith(prefix) == true }.toList()
        check(shown.isNotEmpty()) { "No text starting with '$prefix'" }
        return shown.all { component -> fits(component) && inside(root, component) }
    }

    private fun fits(component: Component): Boolean = when (component) {
        is JLabel -> {
            val insets = component.insets
            component.getFontMetrics(component.font).stringWidth(component.text) <=
                component.width - insets.left - insets.right
        }

        is JTextComponent -> {
            val last = component.modelToView2D(component.document.length)
            last != null && last.maxY <= component.height - component.insets.bottom
        }

        else -> false
    }

    private fun inside(root: Component, component: Component): Boolean {
        val bounds = SwingUtilities.convertRectangle(component.parent, component.bounds, root)
        return bounds.x >= 0 && bounds.y >= 0 && bounds.maxX <= root.width && bounds.maxY <= root.height
    }

    private fun textOf(component: Component): String? = when (component) {
        is JLabel -> component.text
        is JTextComponent -> component.text
        else -> null
    }

    private fun descendants(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(descendants(it)) }
    }

    private const val WORD = 12
}
