package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.run.PresetRunConfiguration
import app.anicanon.kumikae.as3.run.SimulatorRunConfiguration
import app.anicanon.kumikae.as3.settings.GameTargets
import com.intellij.execution.RunManagerListener
import com.intellij.execution.RunnerAndConfigurationSettings
import com.intellij.execution.configurations.RunConfigurationBase
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.progress.ProcessCanceledException
import com.intellij.openapi.progress.ProgressManager
import com.intellij.openapi.util.Key
import com.intellij.openapi.util.ThrowableComputable
import com.intellij.openapi.vfs.LocalFileSystem

/** Create the Godot preset when the user commits a new run configuration. */
internal class NewGameTargetListener : RunManagerListener {
    override fun runConfigurationAdded(settings: RunnerAndConfigurationSettings) {
        val config = settings.configuration as? RunConfigurationBase<*> ?: return
        if (settings.isTemplate) return
        val pending = config.getUserData(PENDING) ?: return
        config.putUserData(PENDING, null)
        val project = config.project
        try {
            when (config) {
                is PresetRunConfiguration -> pending.setup.ensurePreset(
                    config.platform,
                    config.options.preset,
                    pending.identifier,
                )

                is SimulatorRunConfiguration -> if (config.options.bundle.isEmpty()) {
                    pending.setup.ensurePreset("iOS", config.options.preset, pending.identifier)
                }
            }
        } catch (cancel: ProcessCanceledException) {
            throw cancel
        } catch (error: Exception) {
            NotificationGroupManager.getInstance().getNotificationGroup("Kumikae AS3")
                .createNotification("Cannot set up game target", error.message.orEmpty(), NotificationType.ERROR)
                .notify(project)
        }
    }

    companion object {
        private data class Pending(val setup: GameTargetSetup, val name: String, val identifier: String)
        private val PENDING = Key.create<Pending>("kumikae.newGameTarget")

        fun prepare(config: RunConfigurationBase<*>, platform: String, current: String): String {
            val project = config.project
            val path = project.basePath ?: return current
            val read = ThrowableComputable<Pending?, Exception> {
                val root = LocalFileSystem.getInstance().findFileByPath(path) ?: return@ThrowableComputable null
                if (root.findChild("project.godot") == null || root.findChild("kas3.gdextension") == null) {
                    return@ThrowableComputable null
                }
                val setup = GameTargetSetup(project, root)
                val identifier = if (platform == "Web") {
                    ""
                } else {
                    setup.defaultIdentifier().takeIf {
                        GameTargets.validIdentifier(platform, it)
                    } ?: ProjectTemplate.defaultBundle(root.name)
                }
                Pending(setup, current.ifEmpty { setup.suggestedPreset(platform) }, identifier)
            }
            val pending = ProgressManager.getInstance()
                .runProcessWithProgressSynchronously(read, "Reading game targets", true, project) ?: return current
            config.putUserData(PENDING, pending)
            return pending.name
        }

        fun isPending(config: RunConfigurationBase<*>) = config.getUserData(PENDING) != null
    }
}
