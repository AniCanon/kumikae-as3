package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.run.KumikaeRunType
import app.anicanon.kumikae.as3.run.SimulatorRunConfiguration
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.GodotConfig
import com.intellij.execution.RunManager
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.ReadonlyStatusHandler
import com.intellij.openapi.vfs.VirtualFile

internal class GameTargetSetup(private val project: Project, private val root: VirtualFile) {
    private val manager = FileDocumentManager.getInstance()
    private fun text(file: VirtualFile): String =
        com.intellij.openapi.application.ReadAction.compute<String, RuntimeException> {
            requireNotNull(manager.getDocument(file)) { "Cannot read ${file.name}." }.text
        }
    private fun file() = root.findChild("export_presets.cfg")
    private fun exports() = file()?.let(::text).orEmpty()
    fun presets() = GameTargets.presets(GodotConfig(exports()))
    fun defaultIdentifier(): String {
        val config = GodotConfig(text(requireNotNull(root.findChild("project.godot"))))
        val stored = config.string("application", "config/kumikae_package")
        if (stored.isNotBlank()) return stored
        val export = GodotConfig(exports())
        for (preset in GameTargets.presets(export)) {
            val key = preset.identifierKey ?: continue
            val identifier = export.string("${preset.section}.options", key)
            if (identifier.isNotBlank()) return identifier
        }
        return ProjectTemplate.defaultBundle(config.string("application", "config/name", root.name))
    }

    fun add(platform: String, name: String, identifier: String) {
        val old = exports()
        val updated = GameTargets.add(old, platform, name, identifier)
        if (platform == "iOS") GameTargets.ios(updated, name)
        if (updated != old) {
            val destination = file() ?: root
            check(
                !ReadonlyStatusHandler.getInstance(project).ensureFilesWritable(listOf(destination)).hasReadonlyFiles(),
            ) {
                "The export presets are read-only."
            }
            WriteCommandAction.runWriteCommandAction(
                project,
                "Add Godot export target",
                null,
                Runnable {
                    check(exports() == old) { "Export presets changed. Open Add Target again." }
                    val file = file() ?: root.createChildData(this, "export_presets.cfg")
                    val doc = requireNotNull(manager.getDocument(file)) { "Cannot edit export_presets.cfg." }
                    doc.setText(updated)
                    manager.saveDocument(doc)
                },
            )
        }
        if (platform == "iOS") selectSimulator(project, name) else selectAndroid(project, name)
    }

    companion object {
        fun selectAndroid(project: Project, name: String) {
            val manager = RunManager.getInstance(project)
            val factory = KumikaeRunType.factory(KumikaeRunType.Kind.ANDROID)
            val chosen = manager.allSettings.firstOrNull {
                (it.configuration as? app.anicanon.kumikae.as3.run.AndroidRunConfiguration)?.options?.preset == name
            } ?: manager.createConfiguration(manager.suggestUniqueName(name, factory.type), factory).also {
                (it.configuration as app.anicanon.kumikae.as3.run.AndroidRunConfiguration).options.preset = name
                manager.addConfiguration(it)
            }
            manager.selectedConfiguration = chosen
        }

        fun selectSimulator(project: Project, name: String) {
            val manager = RunManager.getInstance(project)
            val existing = manager.allSettings.firstOrNull {
                (it.configuration as? SimulatorRunConfiguration)?.options?.preset == name
            }
            val factory = KumikaeRunType.factory(KumikaeRunType.Kind.SIMULATOR)
            val chosen = existing ?: manager.createConfiguration(
                manager.suggestUniqueName("$name Simulator", factory.type),
                factory,
            ).also {
                (it.configuration as SimulatorRunConfiguration).options.preset = name
                manager.addConfiguration(it)
            }
            manager.selectedConfiguration = chosen
        }
    }
}
