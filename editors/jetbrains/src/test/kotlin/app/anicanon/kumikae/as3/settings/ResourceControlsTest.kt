package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
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
        kotlin.test.assertFailsWith<IllegalArgumentException> { draft.copy(mainScene = "res://wrong.gd").write("", "") }
        kotlin.test.assertFailsWith<IllegalArgumentException> { draft.copy(splash = "res://wrong.svg").write("", "") }
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
