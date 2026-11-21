package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.Disposable
import com.intellij.openapi.project.Project
import java.awt.event.HierarchyEvent
import java.awt.event.HierarchyListener

internal class GameDeviceCatalog(
    private val project: Project,
    private val ios: SimulatorRepository?,
    private val android: AndroidEmulators,
    private val openIos: (SimulatorDevice) -> Unit,
    private val openAndroid: (AndroidAvd) -> Unit,
) : Disposable {
    private val iosWorker = BackgroundWorker("iOS simulator list")
    private val androidWorker = BackgroundWorker("Android emulator list")
    private var simulators by mutableStateOf(emptyList<SimulatorDevice>())
    private var emulators by mutableStateOf(emptyList<AndroidAvd>())
    private var iosStatus by mutableStateOf("Loading simulators…")
    private var graphics by mutableStateOf(android.graphics)
    private var androidStatus by mutableStateOf("Loading emulators…")

    private var companionWorker = BackgroundWorker("Companion devices")
    private var groups by mutableStateOf(emptyList<DeviceGroup>())
    private var loadingGroups by mutableStateOf(false)
    private var loadingIos by mutableStateOf(false)
    private var loadingAndroid by mutableStateOf(false)
    val component = composePanel(this) {
        DeviceCatalogView(
            simulators.takeIf { ios != null }, emulators, iosStatus, androidStatus,
            loadingIos || loadingAndroid || loadingGroups,
            graphics, openIos, openAndroid,
            { value ->
                graphics = value
                android.graphics = value
            },
            ::refresh, groups,
        )
    }

    private var showing = false
    private val visibility = HierarchyListener { event ->
        if ((event.changeFlags and HierarchyEvent.SHOWING_CHANGED.toLong()) == 0L) return@HierarchyListener
        val now = component.isShowing
        if (now && !showing) refresh()
        showing = now
    }

    init {
        component.addHierarchyListener(visibility)
        DeviceCatalogContribution.EP.addChangeListener(
            Runnable {
                companionWorker.dispose()
                companionWorker = BackgroundWorker("Companion devices")
                groups = emptyList()
                loadingGroups = false
                if (showing) refreshGroups()
            },
            this,
        )
    }

    private fun refreshGroups() {
        val providers = DeviceCatalogContribution.EP.extensionList
        if (providers.isNotEmpty()) {
            loadingGroups = true
            companionWorker.submit({ providers.map { it.read(project) } }) { result ->
                loadingGroups = false
                result.onSuccess { groups = it }
            }
        } else {
            groups = emptyList()
        }
    }

    private fun refresh() {
        if (loadingIos || loadingAndroid || loadingGroups) return
        refreshGroups()
        ios?.let { repository ->
            loadingIos = true
            iosWorker.submit({
                repository.connect()
                repository.devices()
            }) { result ->
                loadingIos = false
                result.onSuccess { simulators = it }.onFailure { simulators = emptyList() }
                iosStatus =
                    result.exceptionOrNull()?.message ?: if (simulators.isEmpty()) "No installed simulators" else ""
            }
        }
        loadingAndroid = true
        androidWorker.submit(android::list) { result ->
            loadingAndroid = false
            result.onSuccess { emulators = it }.onFailure { emulators = emptyList() }
            androidStatus =
                result.exceptionOrNull()?.message ?: if (emulators.isEmpty()) "No installed emulators" else ""
        }
    }

    override fun dispose() {
        component.removeHierarchyListener(visibility)
        iosWorker.dispose()
        androidWorker.dispose()
        companionWorker.dispose()
    }
}
