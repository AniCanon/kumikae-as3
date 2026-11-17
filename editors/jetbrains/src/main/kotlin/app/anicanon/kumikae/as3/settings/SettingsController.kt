package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.awt.image.BufferedImage

internal interface SettingsRepository {
    fun preview(path: String): BufferedImage? = null
    fun loadPreview(path: String, ready: (BufferedImage?) -> Unit) = ready(preview(path))
    fun load(): ProjectSettings
    fun save(expected: ProjectSettings, draft: ProjectSettings): ProjectSettings
}

internal class SettingsController(private val repository: SettingsRepository) {
    var preview by mutableStateOf<BufferedImage?>(null)
        private set
    var splashPreview by mutableStateOf<BufferedImage?>(null)
        private set
    var exportPreview by mutableStateOf<BufferedImage?>(null)
        private set
    var sceneLabels by mutableStateOf(emptyMap<String, String>())
    private fun previews(value: ProjectSettings, previous: ProjectSettings? = null) {
        fun request(path: String, old: String?, current: () -> String?, assign: (BufferedImage?) -> Unit) {
            if (path == old) return
            assign(null)
            if (path.isNotEmpty()) repository.loadPreview(path) { image -> if (current() == path) assign(image) }
        }
        request(value.icon, previous?.icon, { draft?.icon }) { preview = it }
        request(value.splash, previous?.splash, { draft?.splash }) { splashPreview = it }
        request(value.exportIcon, previous?.exportIcon, { draft?.exportIcon }) { exportPreview = it }
    }
    private var original: ProjectSettings? = null
    var draft by mutableStateOf<ProjectSettings?>(null)
        private set
    var status by mutableStateOf("Loading project settings…")
        private set
    var dirty by mutableStateOf(false)
        private set

    fun reload() {
        runCatching { repository.load() }.onSuccess {
            dirty = false
            original = it
            draft = it
            previews(it)
            status = "Settings are stored in project.godot and export_presets.cfg."
        }.onFailure { status = it.message ?: "Could not load project settings." }
    }

    fun edit(change: (ProjectSettings) -> ProjectSettings) {
        val previous = draft
        draft = draft?.let(change)
        draft?.let { previews(it, previous) }
        val value = draft ?: return
        if (value == previous) return
        dirty =
            runCatching {
                value.write(value.projectText, value.exportText) != (value.projectText to value.exportText)
            }.getOrDefault(true)
        status = if (dirty) "Unsaved changes" else ""
    }

    fun apply() {
        val expected = original ?: return
        val value = draft ?: return
        runCatching { repository.save(expected, value) }.onSuccess {
            dirty = false
            original = it
            draft = it
            previews(it)
            status = "Applied. Rebuild exported apps to use these settings."
        }.onFailure { status = it.message ?: "Could not apply project settings." }
    }
}
