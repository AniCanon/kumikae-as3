package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import org.junit.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class UnicodeInputTest {
    @Test fun acceptsPrintableUnicodeWithoutNormalizingIt() {
        val text = "aA1! é e\u0301 日本語 العربية 👩🏽‍💻"
        assertEquals(listOf(text), prepare(text))
    }

    @Test fun refusesControlsUnpairedSurrogatesAndOversizedText() {
        for (text in listOf("", "line\n", "\u007f", "\u0085", "\uD800", "\uDC00", "a".repeat(4097))) {
            assertFailsWith<IllegalArgumentException>(text) { prepare(text) }
        }
    }
    private fun prepare(text: String) = InputArguments.prepare(
        "text",
        JsonObject().apply { addProperty("text", text) },
        FrameGeometry(),
        1.0,
    )
}
