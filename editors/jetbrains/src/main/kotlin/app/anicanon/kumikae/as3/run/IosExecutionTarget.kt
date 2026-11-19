package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.panel.BackgroundWorker
import app.anicanon.kumikae.as3.simulator.ProcessRunner
import com.intellij.execution.ExecutionTarget
import com.intellij.execution.ExecutionTargetManager
import com.intellij.execution.ExecutionTargetProvider
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.service
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo

internal class IosExecutionTarget(val device: IosDevice) : ExecutionTarget() {
    override fun getId() = "kumikae.ios.physical.${device.id}"
    override fun getDisplayName() = "${device.name} (iPhone/iPad)" + if (device.available) "" else " — ${device.detail}"
    override fun getGroupName() = "iOS Devices"
    override fun getDescription() = device.detail
    override fun getIcon() = PluginFiles.mark
    override fun isReady() = device.available
    override fun canRun(configuration: RunConfiguration) = configuration is SimulatorRunConfiguration
}

internal class IosUnavailableTarget(private val message: String) : ExecutionTarget() {
    override fun getId() = "kumikae.ios.unavailable.$message"
    override fun getDisplayName() = message
    override fun getIcon() = PluginFiles.mark
    override fun isReady() = false
    override fun canRun(configuration: RunConfiguration) = configuration is SimulatorRunConfiguration
}

/** Called by the IDE's native run-target dropdown. Never performs discovery on its caller's thread. */
class IosExecutionTargetProvider : ExecutionTargetProvider() {
    override fun getTargets(project: Project, configuration: RunConfiguration): List<ExecutionTarget> {
        if (configuration !is SimulatorRunConfiguration || !SystemInfo.isMac) return emptyList()
        return project.service<IosTargetService>().targets()
    }
}

@Service(Service.Level.PROJECT)
internal class IosTargetService(private val project: Project) : Disposable {
    private val repository = IosDeviceRepository(ProcessRunner())
    private val controller = IosTargetController(repository::discover, BackgroundWorker("Kumikae iOS discovery"), {
        if (!project.isDisposed) ExecutionTargetManager.getInstance(project).update()
    })
    fun targets(): List<ExecutionTarget> {
        ApplicationManager.getApplication().invokeLater {
            if (!project.isDisposed) controller.refresh()
        }
        val snapshot = controller.snapshot
        return snapshot.devices.map(::IosExecutionTarget) + snapshot.errors.map(::IosUnavailableTarget)
    }

    override fun dispose() = controller.dispose()
}
