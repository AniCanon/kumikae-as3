package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import java.awt.image.BufferedImage
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SettingsControllerTest {
    @Test fun `a late thumbnail cannot replace reloaded artwork at the same path`() {
        val callbacks = mutableListOf<(Artwork?) -> Unit>()
        val repository = object : SettingsRepository {
            override fun load() = ProjectSettings.read("[application]\nconfig/icon=\"res://art.png\"\n", "")
            override fun save(expected: ProjectSettings, draft: ProjectSettings) = draft
            override fun loadPreview(path: String, filter: Boolean, ready: (Artwork?) -> Unit) {
                callbacks.add(ready)
            }
        }
        val controller = SettingsController(repository)
        controller.reload()
        controller.reload()
        val image = Artwork(BufferedImage(32, 32, BufferedImage.TYPE_INT_RGB))
        callbacks.last()(image)
        callbacks.first()(null)
        assertEquals(image, controller.preview)
    }

    @Test fun `export all selects the preset that needs a team before starting a build`() {
        val repository = object : SettingsRepository {
            override fun load() = ProjectSettings.read(
                "",
                "[preset.0]\nname=\"Android\"\nplatform=\"Android\"\n[preset.1]\nname=\"iOS\"\nplatform=\"iOS\"\n",
            )
            override fun save(expected: ProjectSettings, draft: ProjectSettings) = draft
        }
        val controller = SettingsController(repository)
        controller.reload()
        val presets = requireNotNull(controller.draft).presets
        assertTrue(controller.readyToExport(listOf(presets.first())))
        assertEquals(false, controller.readyToExport(presets))
        assertEquals(presets.last().section, controller.draft?.preset)
        assertTrue(controller.status.contains("Choose a device signing team"))
    }

    @Test fun `stale draft cannot overwrite an external edit`() {
        val repository = object : SettingsRepository {
            var value = ProjectSettings.read("[application]\nconfig/name=\"Original\"\n", "")
            override fun load() = value
            override fun save(expected: ProjectSettings, draft: ProjectSettings): ProjectSettings {
                check(expected.projectText == value.projectText) { "Files changed. Reload before applying." }
                val result = draft.write(value.projectText, value.exportText)
                value = ProjectSettings.read(result.first, result.second)
                return value
            }
        }
        val controller = SettingsController(repository)
        controller.reload()
        controller.edit { it.copy(name = "Mine") }
        repository.value = ProjectSettings.read("[application]\nconfig/name=\"Theirs\"\n", "")
        controller.apply()
        assertEquals("Theirs", repository.value.name)
        assertTrue(controller.status.contains("Reload"))
        assertTrue(controller.dirty)
    }

    @Test fun `dirty transitions notify listeners and property edits reconcile with structured fields`() {
        val transitions = mutableListOf<Boolean>()
        val repository = object : SettingsRepository {
            var value = ProjectSettings.read("[application]\nconfig/name=\"Start\"\n", "")
            override fun load() = value
            override fun save(expected: ProjectSettings, draft: ProjectSettings): ProjectSettings {
                val (main, exports) = draft.write(value.projectText, value.exportText)
                value = ProjectSettings.read(main, exports)
                return value
            }
        }
        val controller = SettingsController(repository, onDirtyChanged = { transitions += it })
        controller.reload()
        controller.edit { it.copy(name = "General") }
        controller.edit {
            it.copy(
                propertyEdits =
                it.propertyEdits + (ConfigKey("application", "config/name") to "\"Props\""),
            )
        }
        val (fromProps, _) = requireNotNull(controller.draft).write(repository.value.projectText, "")
        assertEquals("Props", GodotConfig(fromProps).string("application", "config/name"))

        controller.edit { it.copy(name = "GeneralAgain") }
        controller.apply()
        assertEquals("GeneralAgain", repository.value.name)
        assertEquals(listOf(true, false), transitions)
    }

    @Test fun `rejected team submission does not leave teamsBusy stuck`() {
        val repository = object : SettingsRepository {
            override val hasXcodeTeams = true
            override fun loadTeams(ready: (Result<List<SigningTeam>>) -> Unit) = false
            override fun openXcode(ready: (Result<Unit>) -> Unit) = false
            override fun load() = ProjectSettings.read("", "")
            override fun save(expected: ProjectSettings, draft: ProjectSettings) = draft
        }
        val controller = SettingsController(repository)
        controller.refreshTeams()
        assertFalse(controller.teamsBusy)
        controller.openXcode()
        assertFalse(controller.teamsBusy)
    }

    @Test fun `property validation and saved credential parsing reject malformed input`() {
        assertTrue(isValidGodotProperty("application", "config/name", "\"Demo\""))
        assertFalse(isValidGodotProperty("bad]section", "key", "1"))
        assertFalse(isValidGodotProperty("application", "bad=key", "1"))
        assertFalse(isValidGodotProperty("application", "key", "line1\rline2"))
        assertFailsWith<IllegalArgumentException> { AndroidUploadKeys.parseUploadKey("{bad json", "pw") }
        assertFailsWith<IllegalArgumentException> { AndroidUploadKeys.parseUploadKey("[\"only-one\"]", "pw") }
        val validKey = AndroidUploadKeys.parseUploadKey("[\"/tmp/k.p12\",\"upload\"]", "secret")
        assertEquals("/tmp/k.p12", validKey.path)
        assertEquals("upload", validKey.user)
        assertNull(TesterAccess.parseTesterConnection("{corrupt"))
        assertNull(TesterAccess.parseTesterConnection("{}"))
    }
}
