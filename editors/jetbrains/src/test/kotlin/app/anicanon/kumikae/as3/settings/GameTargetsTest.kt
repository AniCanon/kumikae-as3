package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class GameTargetsTest {
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
