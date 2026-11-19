package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.run.AndroidDevices
import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.components.service
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Key
import com.intellij.openapi.util.SystemInfo
import com.intellij.openapi.wm.ToolWindow
import com.intellij.openapi.wm.ToolWindowFactory
import com.intellij.openapi.wm.ToolWindowManager
import com.intellij.ui.content.Content
import com.intellij.ui.content.ContentFactory

class SimulatorToolWindowFactory :
    ToolWindowFactory,
    DumbAware {
    override suspend fun isApplicableAsync(project: Project): Boolean = true

    override fun createToolWindowContent(project: Project, toolWindow: ToolWindow) {
        val ios = if (SystemInfo.isMac) project.service<SimulatorHost>().repository else null
        val emulators = androidEmulators(project)
        val catalog = GameDeviceCatalog(ios, emulators, { device ->
            val content = iosContent(project, toolWindow)
            toolWindow.contentManager.setSelectedContent(content)
            content.getUserData(IOS)?.openDevice(device)
        }, { device ->
            val content = androidContent(project, toolWindow)
            toolWindow.contentManager.setSelectedContent(content)
            content.getUserData(ANDROID)?.openAvd(device)
        })
        val content = ContentFactory.getInstance().createContent(catalog.component, SIMULATORS_TAB, false)
        content.setDisposer(catalog)
        toolWindow.contentManager.addContent(content)
    }

    companion object {
        private const val TOOL_WINDOW_ID = "Game Devices"
        private const val SIMULATORS_TAB = "Simulators"
        private const val IOS_TAB = "iOS"
        private const val ANDROID_TAB = "Android"
        private val IOS = Key.create<SimulatorToolWindow>("kumikae.ios.window")
        private val ANDROID = Key.create<AndroidDeviceWindow>("kumikae.android.window")

        private fun androidEmulators(project: Project) = AndroidEmulators(
            project.service<AndroidDevices>(),
            PropertiesComponent.getInstance(),
        )

        private fun iosContent(project: Project, toolWindow: ToolWindow): Content {
            toolWindow.contentManager.findContent(IOS_TAB)?.let { return it }
            val settings = PropertiesComponent.getInstance()
            val controller = SimulatorComposition.controller(
                project.service<SimulatorHost>().repository,
                inputAllowed = { settings.getBoolean(INPUT_ALLOWED) },
                rememberInput = { settings.setValue(INPUT_ALLOWED, it) },
            )
            val view = SimulatorToolWindow(controller, IdePrompts(project))
            return ContentFactory.getInstance().createContent(view.component, IOS_TAB, false).also {
                it.putUserData(IOS, view)
                it.setDisposer(view.controller)
                toolWindow.contentManager.addContent(it)
            }
        }

        private fun androidContent(project: Project, window: ToolWindow): Content {
            window.contentManager.findContent(ANDROID_TAB)?.let { return it }
            val viewer = AndroidDeviceWindow(androidEmulators(project))
            return ContentFactory.getInstance().createContent(viewer.component, ANDROID_TAB, false).also {
                it.putUserData(ANDROID, viewer)
                it.setDisposer(viewer)
                window.contentManager.addContent(it)
            }
        }

        private fun showTab(project: Project, select: (ToolWindow) -> Content, after: (Content) -> Unit) {
            val window = ToolWindowManager.getInstance(project).getToolWindow(TOOL_WINDOW_ID) ?: return
            window.show {
                val content = select(window)
                window.contentManager.setSelectedContent(content)
                after(content)
            }
        }

        internal fun showIos(project: Project) {
            if (!SystemInfo.isMac) return
            showTab(project, { iosContent(project, it) }) {
                it.getUserData(IOS)?.state?.listShown = false
            }
        }

        internal fun showAndroid(project: Project, serial: String) {
            showTab(project, { androidContent(project, it) }) {
                it.getUserData(ANDROID)?.openSerial(serial)
            }
        }

        // The owner's yes to input through native helper, which uses private Apple interfaces. Asked once.
        const val INPUT_ALLOWED = "kumikae.as3.simulator.inputAllowed"
    }
}
