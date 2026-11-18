package app.anicanon.kumikae.as3.panel

import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.components.service
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import com.intellij.openapi.wm.ToolWindow
import com.intellij.openapi.wm.ToolWindowFactory
import com.intellij.ui.content.ContentFactory

class SimulatorToolWindowFactory :
    ToolWindowFactory,
    DumbAware {
    // The service drives Xcode's simctl, which exists only on macOS.
    override suspend fun isApplicableAsync(project: Project): Boolean = true

    override fun createToolWindowContent(project: Project, toolWindow: ToolWindow) {
        val ios = if (SystemInfo.isMac) project.service<SimulatorHost>().repository else null
        val emulators = AndroidEmulators(project.service<app.anicanon.kumikae.as3.run.AndroidDevices>())
        val catalog = GameDeviceCatalog(ios, emulators, { device ->
            val content = toolWindow.contentManager.findContent("iOS") ?: addIos(project, toolWindow)
            toolWindow.contentManager.setSelectedContent(content)
            content.getUserData(IOS)?.openDevice(device)
        }, { device ->
            val content = androidContent(project, toolWindow)
            toolWindow.contentManager.setSelectedContent(content)
            content.getUserData(ANDROID)?.openAvd(device)
        })
        val content = ContentFactory.getInstance().createContent(catalog.component, "Simulators", false)
        content.setDisposer(catalog)
        toolWindow.contentManager.addContent(content)
    }

    private fun addIos(project: Project, toolWindow: ToolWindow): com.intellij.ui.content.Content {
        val settings = PropertiesComponent.getInstance()
        val controller = SimulatorComposition.controller(
            project.service<SimulatorHost>().repository,
            inputAllowed = { settings.getBoolean(INPUT_ALLOWED) },
            rememberInput = { settings.setValue(INPUT_ALLOWED, it) },
        )
        val view = SimulatorToolWindow(controller, IdePrompts(project))
        val content = ContentFactory.getInstance().createContent(view.component, "iOS", false)
        content.putUserData(IOS, view)
        content.setDisposer(view.controller)
        toolWindow.contentManager.addContent(content)
        return content
    }

    companion object {
        private val IOS = com.intellij.openapi.util.Key.create<SimulatorToolWindow>("kumikae.ios.window")
        internal fun showIos(project: Project) {
            val window =
                com.intellij.openapi.wm.ToolWindowManager.getInstance(project).getToolWindow("Game Devices") ?: return
            window.show {
                val content =
                    window.contentManager.findContent("iOS") ?: SimulatorToolWindowFactory().addIos(project, window)
                window.contentManager.setSelectedContent(content)
                content.getUserData(IOS)?.state?.listShown = false
            }
        }
        private fun androidContent(project: Project, window: ToolWindow): com.intellij.ui.content.Content {
            window.contentManager.findContent("Android")?.let { return it }
            val viewer =
                AndroidDeviceWindow(AndroidEmulators(project.service<app.anicanon.kumikae.as3.run.AndroidDevices>()))
            return ContentFactory.getInstance().createContent(viewer.component, "Android", false).also {
                it.putUserData(ANDROID, viewer)
                it.setDisposer(viewer)
                window.contentManager.addContent(it)
            }
        }
        private val ANDROID = com.intellij.openapi.util.Key.create<AndroidDeviceWindow>("kumikae.android.window")
        internal fun showAndroid(project: Project, serial: String) {
            val window =
                com.intellij.openapi.wm.ToolWindowManager.getInstance(project).getToolWindow("Game Devices") ?: return
            window.show {
                val content = androidContent(project, window)
                window.contentManager.setSelectedContent(content)
                content.getUserData(ANDROID)?.openSerial(serial)
            }
        }

        // The owner's yes to input through native helper, which uses private Apple interfaces. Asked once.
        const val INPUT_ALLOWED = "kumikae.as3.simulator.inputAllowed"
    }
}
