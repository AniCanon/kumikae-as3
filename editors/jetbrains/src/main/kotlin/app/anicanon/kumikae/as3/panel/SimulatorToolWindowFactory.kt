package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.application.PathManager
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import com.intellij.openapi.wm.ToolWindow
import com.intellij.openapi.wm.ToolWindowFactory
import com.intellij.ui.content.ContentFactory
import java.nio.file.Path

class SimulatorToolWindowFactory :
    ToolWindowFactory,
    DumbAware {
    // The service drives Xcode's simctl, which exists only on macOS.
    override suspend fun isApplicableAsync(project: Project): Boolean = SystemInfo.isMac

    override fun createToolWindowContent(project: Project, toolWindow: ToolWindow) {
        val controller = SimulatorComposition.controller(Path.of(PathManager.getSystemPath(), "kumikae-as3"))
        val view = SimulatorToolWindow(controller, IdePrompts(project))
        val content = ContentFactory.getInstance().createContent(view.component, "", false)
        content.setDisposer(view.controller)
        toolWindow.contentManager.addContent(content)
    }
}
