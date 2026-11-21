package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

internal class GameDeviceCatalogController(
    private val ios: SimulatorRepository?,
    private val listAndroid: () -> List<AndroidAvd>,
    initialGraphics: AndroidGraphics,
    private val persistGraphics: (AndroidGraphics) -> Unit,
    private val readCompanions: () -> List<DeviceGroup>,
    private val hasCompanions: () -> Boolean,
    private val iosWorker: BackgroundTasks,
    private val androidWorker: BackgroundTasks,
    private var companionWorker: BackgroundTasks,
    private val newCompanionWorker: () -> BackgroundTasks = { companionWorker },
) {
    var simulators by mutableStateOf(emptyList<SimulatorDevice>())
        private set
    var emulators by mutableStateOf(emptyList<AndroidAvd>())
        private set
    var iosStatus by mutableStateOf("Loading simulators…")
        private set
    var graphics by mutableStateOf(initialGraphics)
        private set
    var androidStatus by mutableStateOf("Loading emulators…")
        private set
    var groups by mutableStateOf(emptyList<DeviceGroup>())
        private set

    private var loadingGroups by mutableStateOf(false)
    private var loadingIos by mutableStateOf(false)
    private var loadingAndroid by mutableStateOf(false)
    private var companionGeneration = 0
    private var showing = false
    private var disposed = false

    val refreshing: Boolean
        get() = loadingIos || loadingAndroid || loadingGroups

    fun selectGraphics(value: AndroidGraphics) {
        graphics = value
        persistGraphics(value)
    }

    fun onVisibilityChanged(visible: Boolean) {
        if (disposed) return
        if (visible && !showing) refresh()
        showing = visible
    }

    fun onCompanionsChanged() {
        if (disposed) return
        companionGeneration++
        companionWorker.dispose()
        companionWorker = newCompanionWorker()
        groups = emptyList()
        loadingGroups = false
        if (showing) refreshGroups()
    }

    fun refresh() {
        if (disposed || refreshing) return
        refreshGroups()
        ios?.let { repository ->
            loadingIos = true
            val submitted = iosWorker.submit({
                repository.connect()
                repository.devices()
            }) { result ->
                loadingIos = false
                if (disposed) return@submit
                result.onSuccess { simulators = it }.onFailure { simulators = emptyList() }
                iosStatus =
                    result.exceptionOrNull()?.message ?: if (simulators.isEmpty()) "No installed simulators" else ""
            }
            if (!submitted) loadingIos = false
        }
        loadingAndroid = true
        val submitted = androidWorker.submit(listAndroid) { result ->
            loadingAndroid = false
            if (disposed) return@submit
            result.onSuccess { emulators = it }.onFailure { emulators = emptyList() }
            androidStatus =
                result.exceptionOrNull()?.message ?: if (emulators.isEmpty()) "No installed emulators" else ""
        }
        if (!submitted) loadingAndroid = false
    }

    private fun refreshGroups() {
        if (!hasCompanions()) {
            groups = emptyList()
            return
        }
        val generation = companionGeneration
        loadingGroups = true
        val submitted = companionWorker.submit(readCompanions) { result ->
            if (disposed || generation != companionGeneration) return@submit
            loadingGroups = false
            result.onSuccess { groups = it }
        }
        if (!submitted) loadingGroups = false
    }

    fun dispose() {
        disposed = true
        iosWorker.dispose()
        androidWorker.dispose()
        companionWorker.dispose()
    }
}
