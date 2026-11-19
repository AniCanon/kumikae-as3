package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class GodotConfigTest {
    @Test fun structuredPropertiesStayIntactAcrossSettingsEdits() {
        val raw = """[input]
jump={
"events": [
[1, 2],
[3, 4]
],
"label": "a # literal ]"
} ; end

[application]
config/name="Before"
"""
        val config = GodotConfig(raw)
        assertEquals(raw.substringAfter("jump=").substringBefore(" ; end"), config.raw("input", "jump"))
        assertEquals(
            raw.replace("Before", "After"),
            config.update(mapOf(ConfigKey("application", "config/name") to "\"After\"")),
        )
    }

    @Test fun `editing preserves unrelated source and comments`() {
        val text = "; retained\r\nconfig_version=5\r\n[application]\r\n" +
            "config/name=\"Old\" ; note\r\nunknown=Vector2(1, 2)\r\n"
        val edited = GodotConfig(text).update(mapOf(ConfigKey("application", "config/name") to "\"New\""))
        assertEquals(text.replace("\"Old\"", "\"New\""), edited)
    }

    @Test fun `missing sections are inserted and strings round trip`() {
        val value = "A \"name\" \\ path\nnext"
        val edited = GodotConfig("config_version=5\n").update(
            mapOf(ConfigKey("application", "config/name") to GodotConfig.quote(value)),
        )
        assertEquals(value, GodotConfig(edited).string("application", "config/name"))
    }

    @Test fun `standard Godot string escapes decode cleanly`() {
        assertEquals("it's \u00e9", GodotConfig.unquote("\"it\\'s \\u00e9\""))
    }

    @Test fun `ambiguous keys and malformed values are refused`() {
        assertFailsWith<IllegalArgumentException> { GodotConfig("[display]\na=1\na=2\n") }
        assertFailsWith<IllegalArgumentException> { GodotConfig("[display\na=1") }
        assertFailsWith<IllegalArgumentException> {
            GodotConfig("[application]\nconfig/name=\"open").string("application", "config/name")
        }
    }
}
