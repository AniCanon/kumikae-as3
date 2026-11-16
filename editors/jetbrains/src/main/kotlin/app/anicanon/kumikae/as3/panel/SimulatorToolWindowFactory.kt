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
    override suspend fun isApplicableAsync(project: Project): Boolean = SystemInfo.isMac

    override fun createToolWindowContent(project: Project, toolWindow: ToolWindow) {
        val settings = PropertiesComponent.getInstance()
        val controller = SimulatorComposition.controller(
            project.service<SimulatorHost>().repository,
            inputAllowed = { settings.getBoolean(INPUT_ALLOWED) },
            rememberInput = { settings.setValue(INPUT_ALLOWED, it) },
        )
        val view = SimulatorToolWindow(controller, IdePrompts(project))
        val content = ContentFactory.getInstance().createContent(view.component, "", false)
        content.setDisposer(view.controller)
        toolWindow.contentManager.addContent(content)
    }

    private companion object {
        // The owner's yes to input through native helper, which uses private Apple interfaces. Asked once.
        const val INPUT_ALLOWED = "kumikae.as3.simulator.inputAllowed"
    }
}
