package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
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
        val image = Artwork(java.awt.image.BufferedImage(32, 32, java.awt.image.BufferedImage.TYPE_INT_RGB))
        callbacks.last()(image)
        callbacks.first()(null)
        assertEquals(image, controller.preview)
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
