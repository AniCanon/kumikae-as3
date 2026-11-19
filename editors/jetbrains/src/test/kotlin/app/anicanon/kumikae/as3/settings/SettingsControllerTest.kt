package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import java.awt.image.BufferedImage
import kotlin.test.assertEquals
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
}
