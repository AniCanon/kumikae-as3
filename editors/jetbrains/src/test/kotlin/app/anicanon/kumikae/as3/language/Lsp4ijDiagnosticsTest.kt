package app.anicanon.kumikae.as3.language

import com.intellij.lang.annotation.AnnotationBuilder
import com.intellij.lang.annotation.AnnotationHolder
import com.intellij.openapi.editor.impl.DocumentImpl
import com.intellij.openapi.util.TextRange
import org.eclipse.lsp4j.Diagnostic
import org.eclipse.lsp4j.DiagnosticSeverity
import org.eclipse.lsp4j.Position
import org.eclipse.lsp4j.Range
import org.junit.jupiter.api.Test
import java.lang.reflect.Proxy
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class Lsp4ijDiagnosticsTest {
    @Test fun `deleted lines and shortened columns cannot reach annotation conversion`() {
        val document = document("short")
        for (range in listOf(range(1, 0, 1, 1), range(0, 2, 0, 9), range(0, 9, 0, 9))) {
            assertFalse(diagnosticRangeFits(range, document), "$range")
        }
    }

    @Test fun `negative and reversed positions are rejected`() {
        for (range in listOf(range(-1, 0, 0, 0), range(0, -1, 0, 0), range(1, 0, 0, 0), range(0, 3, 0, 2))) {
            assertFalse(diagnosticRangeFits(range, document("first", "second")), "$range")
        }
    }

    @Test fun `valid multiline and zero width ranges retain UTF16 and line end positions`() {
        val document = document("a😀b", "", "end")
        for (range in listOf(range(0, 1, 0, 3), range(0, 4, 0, 4), range(0, 0, 2, 3), range(1, 0, 1, 0))) {
            assertTrue(diagnosticRangeFits(range, document), "$range")
        }
        assertTrue(diagnosticRangeFits(range(0, 0, 0, 0), document("")))
        assertFalse(diagnosticRangeFits(range(0, 0, 0, 1), document("")))
        assertFalse(diagnosticRangeFits(range(0, 0, 1, 0), document("")))
    }

    @Test fun `empty document diagnostic renders at the origin without line lookup`() {
        var created = false
        var renderedRange: TextRange? = null
        val builder = Proxy.newProxyInstance(
            AnnotationBuilder::class.java.classLoader,
            arrayOf(AnnotationBuilder::class.java),
        ) { proxy, method, args ->
            when (method.name) {
                "range" -> {
                    renderedRange = args!![0] as TextRange
                    proxy
                }

                "create" -> {
                    created = true
                    null
                }

                else -> proxy
            }
        } as AnnotationBuilder
        val holder = Proxy.newProxyInstance(
            AnnotationHolder::class.java.classLoader,
            arrayOf(AnnotationHolder::class.java),
        ) { _, method, args ->
            check(method.name == "newAnnotation")
            assertEquals("File diagnostic", args!![1])
            builder
        } as AnnotationHolder
        val diagnostic = Diagnostic(range(0, 0, 0, 0), "File diagnostic").apply {
            severity = DiagnosticSeverity.Error
        }
        val empty = document("")
        assertEquals(0, empty.lineCount)
        As3Lsp4ijDiagnostics().createAnnotation(diagnostic, empty, emptyList(), holder)
        assertTrue(created)
        assertEquals(TextRange.EMPTY_RANGE, renderedRange)
    }

    private fun range(startLine: Int, start: Int, endLine: Int, end: Int) =
        Range(Position(startLine, start), Position(endLine, end))

    private fun document(vararg lines: String) = DocumentImpl(lines.joinToString("\n"))
}
