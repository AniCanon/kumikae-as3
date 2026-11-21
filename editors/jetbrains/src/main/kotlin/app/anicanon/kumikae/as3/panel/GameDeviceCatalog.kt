package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
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
    private val controller = GameDeviceCatalogController(
        ios = ios,
        listAndroid = android::list,
        initialGraphics = android.graphics,
        persistGraphics = { android.graphics = it },
        readCompanions = { DeviceCatalogContribution.EP.extensionList.map { it.read(project) } },
        hasCompanions = { DeviceCatalogContribution.EP.extensionList.isNotEmpty() },
        iosWorker = BackgroundWorker("iOS simulator list"),
        androidWorker = BackgroundWorker("Android emulator list"),
        companionWorker = BackgroundWorker("Companion devices"),
        newCompanionWorker = { BackgroundWorker("Companion devices") },
    )

    val component = composePanel(this) {
        DeviceCatalogView(
            controller.simulators.takeIf { ios != null },
            controller.emulators,
            controller.iosStatus,
            controller.androidStatus,
            controller.refreshing,
            controller.graphics,
            openIos,
            openAndroid,
            controller::selectGraphics,
            controller::refresh,
            controller.groups,
        )
    }

    private val visibility = HierarchyListener { event ->
        if ((event.changeFlags and HierarchyEvent.SHOWING_CHANGED.toLong()) == 0L) return@HierarchyListener
        controller.onVisibilityChanged(component.isShowing)
    }

    init {
        component.addHierarchyListener(visibility)
        DeviceCatalogContribution.EP.addChangeListener(
            Runnable {
                ApplicationManager.getApplication().invokeLater { controller.onCompanionsChanged() }
            },
            this,
        )
    }

    override fun dispose() {
        component.removeHierarchyListener(visibility)
        controller.dispose()
    }
}
