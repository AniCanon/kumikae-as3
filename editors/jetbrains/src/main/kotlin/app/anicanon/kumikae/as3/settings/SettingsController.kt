package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.intellij.openapi.progress.ProcessCanceledException

internal interface SettingsRepository {
    val hasXcodeTeams: Boolean get() = false
    fun loadTeams(ready: (Result<List<SigningTeam>>) -> Unit): Boolean {
        ready(Result.success(emptyList()))
        return true
    }
    fun openXcode(ready: (Result<Unit>) -> Unit): Boolean {
        ready(Result.success(Unit))
        return true
    }
    fun preview(path: String): Artwork? = null
    fun loadPreview(path: String, filter: Boolean, ready: (Artwork?) -> Unit) = ready(preview(path))
    fun load(): ProjectSettings
    fun save(expected: ProjectSettings, draft: ProjectSettings): ProjectSettings
}

internal class SettingsController(
    private val repository: SettingsRepository,
    private val onDirtyChanged: (Boolean) -> Unit = {},
) {
    var preview by mutableStateOf<Artwork?>(null)
        private set
    var splashPreview by mutableStateOf<Artwork?>(null)
        private set
    var exportPreview by mutableStateOf<Artwork?>(null)
        private set
    var sceneLabels by mutableStateOf(emptyMap<String, String>())
    private var previewGeneration = 0L
    private fun previews(value: ProjectSettings, previous: ProjectSettings? = null) {
        if (previous == null) previewGeneration++
        val generation = previewGeneration
        fun request(
            path: String,
            old: String?,
            current: () -> String?,
            filter: Boolean = true,
            assign: (Artwork?) -> Unit,
        ) {
            if (path == old) return
            assign(null)
            if (path.isNotEmpty()) {
                repository.loadPreview(path, filter) { image ->
                    if (generation == previewGeneration && current() == path) assign(image)
                }
            }
        }
        request(value.icon, previous?.icon, { draft?.icon }) { preview = it }
        if (previous == null || value.splash != previous.splash) {
            request(value.splash, previous?.splash, { draft?.splash }, SplashLayout.read(value).filter) {
                splashPreview = it
            }
        }
        request(value.exportIcon, previous?.exportIcon, { draft?.exportIcon }) { exportPreview = it }
    }
    val hasXcodeTeams get() = repository.hasXcodeTeams
    var teams by mutableStateOf(emptyList<SigningTeam>())
        private set
    var teamStatus by mutableStateOf("")
        private set
    var teamsBusy by mutableStateOf(false)
        private set
    private var requestedTeams = false

    fun refreshTeams() {
        if (teamsBusy || !hasXcodeTeams) return
        requestedTeams = true
        teamsBusy = true
        teamStatus = "Looking for teams in Xcode…"
        val accepted = repository.loadTeams { result ->
            teamsBusy = false
            result.onSuccess {
                teams = it
                teamStatus =
                    if (it.isEmpty()) "Add an account in Xcode → Settings → Apple Accounts, then refresh." else ""
            }.onFailure { teamStatus = "Couldn’t read Xcode teams. Refresh or enter the team ID." }
        }
        if (!accepted) {
            teamsBusy = false
            teamStatus = ""
        }
    }

    fun openXcode() {
        if (teamsBusy || !hasXcodeTeams) return
        teamsBusy = true
        val accepted = repository.openXcode { result ->
            teamsBusy = false
            teamStatus = if (result.isSuccess) {
                "In Xcode, open Settings → Apple Accounts. Refresh here after signing in."
            } else {
                "Couldn’t open Xcode. Check that Xcode is installed on this Mac."
            }
        }
        if (!accepted) teamsBusy = false
    }

    private var original: ProjectSettings? = null
    var draft by mutableStateOf<ProjectSettings?>(null)
        private set
    var status by mutableStateOf("Loading project settings…")
        private set
    var dirty by mutableStateOf(false)
        private set

    private fun updateDirty(value: Boolean) {
        if (dirty == value) return
        dirty = value
        onDirtyChanged(value)
    }

    fun reload() {
        try {
            val loaded = repository.load()
            updateDirty(false)
            original = loaded
            draft = loaded
            previews(loaded)
            if (!requestedTeams && loaded.presets.any { it.platform == "iOS" }) refreshTeams()
            status = ""
        } catch (canceled: ProcessCanceledException) {
            throw canceled
        } catch (error: Exception) {
            status = error.message ?: "Could not load project settings."
        }
    }

    fun edit(change: (ProjectSettings) -> ProjectSettings) {
        val previous = draft
        val updated = draft?.let(change)
        draft = if (previous != null && updated != null) reconcilePropertyEdits(previous, updated) else updated
        draft?.let { previews(it, previous) }
        val value = draft ?: return
        if (value == previous) return
        val nextDirty = try {
            value.write(value.projectText, value.exportText) != (value.projectText to value.exportText)
        } catch (canceled: ProcessCanceledException) {
            throw canceled
        } catch (_: Exception) {
            true
        }
        updateDirty(nextDirty)
        status = if (dirty) "Unsaved changes" else ""
    }

    private fun reconcilePropertyEdits(previous: ProjectSettings, updated: ProjectSettings): ProjectSettings {
        if (updated.propertyEdits.isEmpty()) return updated
        val superseded = mutableListOf<ConfigKey>()
        if (updated.name != previous.name) superseded += ConfigKey("application", "config/name")
        if (updated.version != previous.version) superseded += ConfigKey("application", "config/version")
        if (updated.mainScene != previous.mainScene) superseded += ConfigKey("application", "run/main_scene")
        if (updated.icon != previous.icon) superseded += ConfigKey("application", "config/icon")
        if (updated.splash != previous.splash) superseded += ConfigKey("application", "boot_splash/image")
        if (updated.stretch != previous.stretch) superseded += ConfigKey("display", "window/stretch/mode")
        if (updated.aspect != previous.aspect) superseded += ConfigKey("display", "window/stretch/aspect")
        if (updated.width != previous.width) superseded += ConfigKey("display", "window/size/viewport_width")
        if (updated.height != previous.height) superseded += ConfigKey("display", "window/size/viewport_height")
        if (updated.orientation != previous.orientation) {
            superseded += ConfigKey("display", "window/handheld/orientation")
        }
        if (superseded.isEmpty()) return updated
        return updated.copy(propertyEdits = updated.propertyEdits - superseded.toSet())
    }

    fun readyToExport(presets: List<ExportPreset>): Boolean {
        val value = draft ?: return false
        val missing = presets.firstOrNull { it.platform == "iOS" && value.select(it.section).signingTeam.isBlank() }
            ?: return true
        edit { it.select(missing.section) }
        status = "Choose a device signing team for ${missing.name}, then click Apply."
        return false
    }

    fun apply() {
        val expected = original ?: return
        val value = draft ?: return
        try {
            val saved = repository.save(expected, value)
            updateDirty(false)
            original = saved
            draft = saved
            previews(saved)
            status = "Applied. Rebuild exported apps to use these settings."
        } catch (canceled: ProcessCanceledException) {
            throw canceled
        } catch (error: Exception) {
            status = error.message ?: "Could not apply project settings."
        }
    }
}
