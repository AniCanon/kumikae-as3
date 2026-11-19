package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.panel.BackgroundWorker
import app.anicanon.kumikae.as3.panel.SimulatorDevice
import app.anicanon.kumikae.as3.panel.SimulatorHost
import com.intellij.execution.ExecutionTarget
import com.intellij.execution.ExecutionTargetManager
import com.intellij.execution.ExecutionTargetProvider
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.icons.AllIcons
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.Disposable
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.service
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import java.util.concurrent.TimeUnit

internal class SimulatorDestination(val device: SimulatorDevice) : ExecutionTarget() {
    override fun getId() = "kumikae.ios.${device.id}"
    override fun getDisplayName() = "${device.name} · ${device.runtime}"
    override fun getDescription() = device.state
    override fun getIcon() = AllIcons.Nodes.Console
    override fun canRun(configuration: RunConfiguration) = configuration is SimulatorRunConfiguration
}

class SimulatorDestinations : ExecutionTargetProvider() {
    override fun getTargets(project: Project, configuration: RunConfiguration): List<ExecutionTarget> =
        if (configuration is SimulatorRunConfiguration && SystemInfo.isMac) {
            project.service<SimulatorDeviceList>().targets()
        } else {
            emptyList()
        }
}

@Service(Service.Level.PROJECT)
internal class SimulatorDeviceList(private val project: Project) : Disposable {
    private val tasks = BackgroundWorker("Kumikae simulator destinations")

    @Volatile private var devices = emptyList<SimulatorDevice>()
    private var refreshAt = 0L
    private var lastError: String? = null

    @Synchronized fun targets(): List<ExecutionTarget> {
        val now = System.nanoTime()
        if (refreshAt == 0L || now - refreshAt >= 0) {
            val submitted = tasks.submit({
                val repository = project.service<SimulatorHost>().repository
                repository.connect()
                repository.devices()
            }) { result ->
                if (project.isDisposed) return@submit
                result.onSuccess {
                    lastError = null
                    if (it != devices) {
                        devices = it
                        ExecutionTargetManager.getInstance(project).update()
                    }
                }.onFailure {
                    val message = it.message ?: "Cannot list simulators."
                    if (message != lastError) {
                        NotificationGroupManager.getInstance().getNotificationGroup("Kumikae AS3")
                            .createNotification(message, NotificationType.WARNING).notify(project)
                    }
                    lastError = message
                }
            }
            if (submitted) refreshAt = now + TimeUnit.SECONDS.toNanos(5)
        }
        return devices.map(::SimulatorDestination)
    }

    override fun dispose() = tasks.dispose()
}
