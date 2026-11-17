package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class GameIdentityTest {
    @Test fun `game version and splash round trip through Godot`() {
        val original = "[application]\nconfig/version=\"1.2\"\nboot_splash/image=\"res://old.png\"\n; preserve me\n"
        val identity = ProjectSettings.read(original, "")
        assertEquals("1.2", identity.version)
        val saved = identity.copy(splash = "res://new.png", version = "1.3").write(original, "").first
        assertTrue(saved.contains("; preserve me"))
        val read = ProjectSettings.read(saved, "")
        assertEquals("res://new.png", read.splash)
        assertEquals("1.3", read.version)
    }
}
