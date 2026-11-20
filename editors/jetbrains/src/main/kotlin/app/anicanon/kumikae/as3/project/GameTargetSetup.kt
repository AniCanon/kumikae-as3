package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.run.AndroidRunConfiguration
import app.anicanon.kumikae.as3.run.KumikaeRunType
import app.anicanon.kumikae.as3.run.SimulatorRunConfiguration
import app.anicanon.kumikae.as3.run.WebRunConfiguration
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.GodotConfig
import com.intellij.execution.RunManager
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.ReadonlyStatusHandler
import com.intellij.openapi.vfs.VirtualFile

internal class GameTargetSetup(private val project: Project, private val root: VirtualFile) {
    private val manager = FileDocumentManager.getInstance()
    private fun text(file: VirtualFile): String = ReadAction.compute<String, RuntimeException> {
        requireNotNull(manager.getDocument(file)) { "Cannot read ${file.name}." }.text
    }
    private fun file() = root.findChild("export_presets.cfg")
    private fun exports() = file()?.let(::text).orEmpty()
    fun presets() = GameTargets.presets(GodotConfig(exports()))
    fun defaultIdentifier(): String {
        val projectFile = requireNotNull(root.findChild("project.godot")) { "Missing project.godot." }
        val config = GodotConfig(text(projectFile))
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
        savePreset(old, platform, name, identifier)
        when (platform) {
            "iOS" -> selectSimulator(project, name)
            "Web" -> selectWeb(project, name)
            "Android" -> selectAndroid(project, name)
            else -> error("Unsupported Godot target platform: $platform")
        }
    }

    fun suggestedPreset(platform: String) = GameTargets.suggestedPreset(GodotConfig(exports()), platform)

    fun ensurePreset(platform: String, name: String, identifier: String) {
        val old = exports()
        if (GameTargets.presets(GodotConfig(old)).any { it.platform == platform && it.name == name }) return
        savePreset(old, platform, name, identifier)
    }

    private fun savePreset(old: String, platform: String, name: String, identifier: String) {
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
    }

    companion object {
        private inline fun <reified T> selectPreset(
            project: Project,
            kind: KumikaeRunType.Kind,
            presetName: String,
            displayName: String,
            presetOf: (T) -> String,
            assign: (T) -> Unit,
        ) {
            val manager = RunManager.getInstance(project)
            val factory = KumikaeRunType.factory(kind)
            val chosen = manager.allSettings.firstOrNull {
                val config = it.configuration as? T
                config != null && presetOf(config) == presetName
            } ?: manager.createConfiguration(manager.suggestUniqueName(displayName, factory.type), factory).also {
                assign(it.configuration as T)
                manager.addConfiguration(it)
            }
            manager.selectedConfiguration = chosen
        }

        fun selectWeb(project: Project, name: String) {
            selectPreset<WebRunConfiguration>(project, KumikaeRunType.Kind.WEB, name, name, { it.options.preset }) {
                it.options.preset = name
            }
        }

        fun selectAndroid(project: Project, name: String) {
            selectPreset<AndroidRunConfiguration>(
                project,
                KumikaeRunType.Kind.ANDROID,
                presetName = name,
                displayName = name,
                presetOf = { it.options.preset },
                assign = { it.options.preset = name },
            )
        }

        fun selectSimulator(project: Project, name: String) {
            selectPreset<SimulatorRunConfiguration>(
                project,
                KumikaeRunType.Kind.SIMULATOR,
                presetName = name,
                displayName = name,
                presetOf = { it.options.preset },
                assign = { it.options.preset = name },
            )
        }
    }
}
