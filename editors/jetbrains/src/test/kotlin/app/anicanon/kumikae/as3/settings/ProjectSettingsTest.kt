package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class ProjectSettingsTest {
    @Test fun `store setup recognizes only credentials usable by the selected preset`() {
        val options = "[preset.1.options]\nkeystore/release=\"res://upload.jks\"\nkeystore/release_user=\"upload\"\n"
        val env = mapOf("GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD" to "fixture")
        assertEquals(true, hasGodotUploadKey("preset.1", "", options, env))
        assertEquals(false, hasGodotUploadKey("preset.0", "", options, env))
        assertEquals(false, hasGodotUploadKey("preset.1", options, "", emptyMap()))
    }

    @Test fun `dedicated distribution chooses the store product independently of the run preset`() {
        val settings = ProjectSettings.read(
            "",
            "[preset.0]\nname=\"Phone\"\nplatform=\"Android\"\nexport_path=\"build/demo.apk\"\n",
        )
        assertEquals("demo.apk", exportJobs(settings, settings.presets, false).single()["filename"])
        assertEquals("demo.aab", exportJobs(settings, settings.presets, true).single()["filename"])
    }

    @Test fun `a generated upload key can be reopened but never overwritten`(@TempDir folder: Path) {
        val file = folder.resolve("upload.p12")
        val key = UploadKey(file.toString(), "upload", "sample-test-password")
        key.create()
        key.validate()
        val bytes = Files.readAllBytes(file)
        assertFailsWith<IllegalArgumentException> { key.create() }
        assertFailsWith<IllegalArgumentException> { UploadKey(file.toString(), "other", key.password).validate() }
        assertContentEquals(bytes, Files.readAllBytes(file))
    }

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

    @Test fun `mobile builds keep their versions in the selected preset`() {
        val text = "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n[preset.1]\nname=\"Android\"\nplatform=\"Android\"\n"
        val settings = ProjectSettings.read("", text).copy(buildNumber = "2.1", releaseVersion = "1.3")
            .select("preset.1").copy(buildNumber = "12", releaseVersion = "1.4")
        val saved = GodotConfig(settings.write("", text).second)
        assertEquals("2.1", saved.string("preset.0.options", "application/version"))
        assertEquals("1.3", saved.string("preset.0.options", "application/short_version"))
        assertEquals("12", saved.raw("preset.1.options", "version/code"))
        assertEquals("1.4", saved.string("preset.1.options", "version/name"))
        assertFailsWith<IllegalArgumentException> { settings.copy(buildNumber = "0").write("", text) }
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
