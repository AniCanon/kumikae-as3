package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.awt.image.BufferedImage

internal interface SettingsRepository {
    fun preview(path: String): BufferedImage? = null
    fun load(): ProjectSettings
    fun save(expected: ProjectSettings, draft: ProjectSettings): ProjectSettings
}

internal class SettingsController(private val repository: SettingsRepository) {
    var preview by mutableStateOf<BufferedImage?>(null)
        private set
    private var original: ProjectSettings? = null
    var draft by mutableStateOf<ProjectSettings?>(null)
        private set
    var status by mutableStateOf("Loading project settings…")
        private set
    val dirty: Boolean get() = draft != original

    fun reload() {
        runCatching { repository.load() }.onSuccess {
            original = it
            draft = it
            preview = repository.preview(it.icon)
            status = "Settings are stored in project.godot and export_presets.cfg."
        }.onFailure { status = it.message ?: "Could not load project settings." }
    }

    fun edit(change: (ProjectSettings) -> ProjectSettings) {
        val previousIcon = draft?.icon
        draft = draft?.let(change)
        if (previousIcon != draft?.icon) preview = draft?.let { repository.preview(it.icon) }
        status = "Unsaved changes · Apply to update the project files."
    }

    fun apply() {
        val expected = original ?: return
        val value = draft ?: return
        runCatching { repository.save(expected, value) }.onSuccess {
            original = it
            draft = it
            preview = repository.preview(it.icon)
            status = "Applied. Rebuild exported apps to use these settings."
        }.onFailure { status = it.message ?: "Could not apply project settings." }
    }
}
