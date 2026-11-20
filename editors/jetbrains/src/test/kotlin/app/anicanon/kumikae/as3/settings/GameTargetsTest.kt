package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class GameTargetsTest {
    @Test fun `new targets use the runnable preset saved by Godot`() {
        val presets = "[preset.0]\nname=\"Web\"\nplatform=\"Web\"\n" +
            "[preset.1]\nname=\"Browser QA\"\nplatform=\"Web\"\n"
        val current = "[runnable_presets]\nWeb=\"Browser QA\"\n"
        assertEquals("Browser QA", GameTargets.suggestedPreset(GodotConfig(current + presets), "Web"))
        assertEquals("Browser QA", GameTargets.suggestedPreset(GodotConfig(presets + "runnable=true\n"), "Web"))
        assertEquals("Web", GameTargets.suggestedPreset(GodotConfig(presets), "Web"))
    }

    @Test fun `web presets enable the extension without a mobile identity`() {
        val config = GodotConfig(GameTargets.add("", "Web", "Browser QA", ""))
        assertEquals("Web", config.string("preset.0", "platform"))
        assertEquals("true", config.raw("preset.0.options", "variant/extensions_support"))
        assertEquals("false", config.raw("preset.0.options", "variant/thread_support"))
        assertEquals("*.as", config.string("preset.0", "include_filter"))
    }

    @Test fun `add reuses existing named presets and launches with the current Godot identifier`() {
        val existing = "[preset.0]\nname=\"iOS\"\nplatform=\"Android\"\n"
        val added = GameTargets.add(existing, "iOS", "QA Phone", "org.example.qa")
        assertTrue(added.startsWith(existing))
        assertEquals(added, GameTargets.add(added, "iOS", "QA Phone", "org.example.other"))
        assertEquals(
            "org.example.changed",
            GameTargets.ios(added.replace("example.qa", "example.changed"), "QA Phone").bundle,
        )
        assertFailsWith<IllegalArgumentException> { GameTargets.add(added, "iOS", "iOS", "org.example.other") }
        assertFailsWith<IllegalArgumentException> { GameTargets.ios(added, "Deleted") }
    }

    @Test fun `identifier validation follows the selected platform in settings and add target`() {
        for ((platform, valid, invalid) in listOf(
            Triple("Android", "org.example.my_game", "org.example.my-game"),
            Triple("iOS", "org.example.my-game", "org.example.my_game"),
        )) {
            val preset = GameTargets.add("", platform, "Development", valid)
            ProjectSettings.read("", preset).write("", preset)
            assertFailsWith<IllegalArgumentException> { GameTargets.add("", platform, "QA", invalid) }
        }
    }

    @Test fun `multiple ios presets need an explicit selection`() {
        val first = GameTargets.add("", "iOS", "Development", "org.example.dev")
        val both = GameTargets.add(first, "iOS", "QA", "org.example.qa")
        assertFailsWith<IllegalArgumentException> { GameTargets.ios(both, "") }
        assertEquals("Development", GameTargets.ios(first, "").name)
    }
}
