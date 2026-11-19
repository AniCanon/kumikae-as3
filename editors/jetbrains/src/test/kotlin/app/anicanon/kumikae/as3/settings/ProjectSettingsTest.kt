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
        assertEquals(IosExecutionMode.Automatic, original.executionMode)
        val edited = original.copy(
            executionMode = IosExecutionMode.Interpreter,
            signingTeam = "EXAMPLE123",
        ).select("preset.1")
        assertEquals(IosExecutionMode.Automatic, edited.executionMode)
        assertEquals("", edited.signingTeam)
        val saved = edited.write("", exports).second
        assertEquals(IosExecutionMode.Interpreter, ProjectSettings.read("", saved).executionMode)
        assertEquals("EXAMPLE123", GodotConfig(saved).string("preset.0.options", "application/app_store_team_id"))
        assertEquals(null, GodotConfig(saved).raw("preset.1.options", "actionscript/native_code"))
    }

    @Test fun `old boolean choices survive unrelated edits and can return to automatic`() {
        for ((flag, mode) in listOf("true" to IosExecutionMode.Native, "false" to IosExecutionMode.Interpreter)) {
            val exports = "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n" +
                "[preset.0.options]\nactionscript/native_code=$flag\n"
            val original = ProjectSettings.read("", exports)
            assertEquals(mode, original.executionMode)
            assertEquals(exports, original.copy(name = "Game").write("", exports).second)
            val automatic = original.copy(executionMode = IosExecutionMode.Automatic).write("", exports).second
            assertEquals("2", GodotConfig(automatic).raw("preset.0.options", "actionscript/native_code"))
        }
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
