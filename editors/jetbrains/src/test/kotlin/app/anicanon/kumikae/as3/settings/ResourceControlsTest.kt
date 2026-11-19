package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ResourceControlsTest {
    @Test fun `the artwork tile opens its picker with a keyboard-accessible action`() {
        var opened = false
        val tile = ArtworkButton("Choose boot splash") { opened = true }
        assertTrue(tile.isFocusable)
        assertEquals("Choose boot splash", tile.accessibleContext.accessibleName)
        assertTrue(tile.icon != null)
        tile.doClick()
        assertTrue(opened)
    }

    @Test fun `only supported project resources appear in each picker`() {
        assertTrue(GameResourceKind.Scene.accepts("level.tscn"))
        assertTrue(GameResourceKind.Scene.accepts("level.scn"))
        assertFalse(GameResourceKind.Scene.accepts("level.gd"))
        assertTrue(GameResourceKind.Icon.accepts("game.svg"))
        assertFalse(GameResourceKind.Splash.accepts("game.svg"))
        assertTrue(GameResourceKind.Splash.accepts("intro.PNG"))
    }

    @Test fun `viewport keeps its proportions and Godot stretch policy`() {
        val draft = ProjectSettings.read("[display]\nwindow/stretch/aspect=\"keep_width\"\n", "")
        assertEquals("keep_width", draft.aspect)
        val saved = draft.copy(aspect = "expand").write(draft.projectText, "").first
        assertEquals("expand", ProjectSettings.read(saved, "").aspect)
        assertEquals("16:9", viewportRatio(1280, 720))
        assertEquals("9:16", viewportRatio(720, 1280))
    }

    @Test fun `typed resources reject scripts and unsupported splash formats on apply`() {
        val draft = ProjectSettings.read("", "")
        assertFailsWith<IllegalArgumentException> { draft.copy(mainScene = "res://wrong.gd").write("", "") }
        assertFailsWith<IllegalArgumentException> { draft.copy(splash = "res://wrong.svg").write("", "") }
        assertFailsWith<IllegalArgumentException> { draft.copy(icon = "res://").write("", "") }
    }

    @Test fun `export drafts use the latest edit for the active preset`() {
        val exports = "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n" +
            "[preset.1]\nname=\"Android\"\nplatform=\"Android\"\n"
        val updated = ProjectSettings.read("", exports)
            .copy(exportIcon = "res://missing.png")
            .select("preset.1")
            .select("preset.0")
            .copy(exportIcon = "res://fixed.png")
        assertEquals("res://fixed.png", updated.allExportDrafts()["preset.0"]?.second)
    }

    @Test fun `applying an identity edit preserves implicit defaults and other presets`() {
        val text = "[application]\nconfig/name=\"Original\"\n"
        val exports = "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n"
        val settings = ProjectSettings.read(text, exports)
        val saved = settings.copy(name = "New").write(text, exports)
        assertEquals(text.replace("Original", "New"), saved.first)
        assertEquals(exports, saved.second)
    }
}
