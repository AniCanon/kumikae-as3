package app.anicanon.kumikae.as3.testing

import org.junit.Test
import kotlin.test.assertEquals

class As3FailureFramesTest {
    @Test fun compilerAndEngineDiagnosticsExposeOnlyTheirSourceCoordinates() {
        val text = """
            Compiling tests
            tests/Bad Score.as:7:21: error: unknown name
            SCRIPT ERROR: Parse Error: bad type
                      at: GDScript::reload (res://tests/a (1).gd:9)
            GDScript backtrace (most recent call first):
                [0] _ready (res://scenes/main.gd:17)
                at check()[tests/Other.as:3]
                at: draw (servers/rendering.cpp:78)
            tests/Bad.as:0:1: error: no line
        """.trimIndent()
        val frames = As3FailureFrames.read(text)
        assertEquals(
            listOf("tests/Bad Score.as:7:21", "res://tests/a (1).gd:9", "res://scenes/main.gd:17", "tests/Other.as:3"),
            frames.map { text.substring(it.start, it.end) },
        )
        assertEquals(listOf(7, 9, 17, 3), frames.map { it.line })
    }
}
