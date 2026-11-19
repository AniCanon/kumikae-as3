package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class ProjectSettingsTest {
    @Test fun `orientation and platform identifiers have distinct storage`() {
        val original = ProjectSettings.read(
            "[application]\nconfig/name=\"Game\"\n",
            "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n[preset.0.options]\napplication/bundle_identifier=\"org.example.game\"\n",
        )
        val draft = original.copy(orientation = 4, identifier = "org.example.newgame", preset = "preset.0")
        val saved = draft.write(original.projectText, original.exportText)
        assertEquals("4", GodotConfig(saved.first).raw("display", "window/handheld/orientation"))
        assertEquals(
            "org.example.newgame",
            GodotConfig(saved.second).string("preset.0.options", "application/bundle_identifier"),
        )
    }

    @Test fun `native compilation and signing team follow the selected iOS preset`() {
        val exports = "[preset.0]\nname=\"Device\"\nplatform=\"iOS\"\n" +
            "[preset.1]\nname=\"Simulator\"\nplatform=\"iOS\"\n"
        val original = ProjectSettings.read("", exports)
        assertEquals(true, original.nativeCode)
        val edited = original.copy(nativeCode = false, signingTeam = "EXAMPLE123").select("preset.1")
        assertEquals(true, edited.nativeCode)
        assertEquals("", edited.signingTeam)
        val saved = edited.write("", exports).second
        assertEquals(false, ProjectSettings.read("", saved).nativeCode)
        assertEquals("EXAMPLE123", GodotConfig(saved).string("preset.0.options", "application/app_store_team_id"))
        assertEquals(null, GodotConfig(saved).raw("preset.1.options", "actionscript/native_code"))
    }

    @Test fun `invalid fields cannot be written`() {
        val value = ProjectSettings.read("", "")
        assertFailsWith<IllegalArgumentException> { value.copy(width = "0").write("", "") }
        assertFailsWith<IllegalArgumentException> { value.copy(icon = "res://../secret.png").write("", "") }
    }

    @Test fun `unknown orientation is not silently converted`() {
        assertFailsWith<IllegalArgumentException> {
            ProjectSettings.read("[display]\nwindow/handheld/orientation=invalid\n", "")
        }
    }

    @Test fun `preset changes retain draft values`() {
        val exports = "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n" +
            "[preset.1]\nname=\"Android\"\nplatform=\"Android\"\n"
        val value = ProjectSettings.read("", exports).copy(identifier = "org.example.edited")
        assertEquals("org.example.edited", value.select("preset.1").select("preset.0").identifier)
    }
}
