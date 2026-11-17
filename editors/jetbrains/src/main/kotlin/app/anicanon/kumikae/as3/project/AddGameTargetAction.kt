package app.anicanon.kumikae.as3.project

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.actionSystem.ActionUpdateThread
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.project.DumbAwareAction
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.DialogWrapper
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import javax.swing.JComponent

class AddGameTargetAction : DumbAwareAction() {
    override fun getActionUpdateThread() = ActionUpdateThread.BGT
    private fun root(project: Project) = project.basePath?.let { LocalFileSystem.getInstance().findFileByPath(it) }
    override fun update(e: AnActionEvent) {
        e.presentation.isEnabledAndVisible = e.project?.let(::root)?.findChild("kas3.gdextension") != null
    }

    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val root = root(project) ?: return
        runCatching {
            AddGameTargetDialog(project, GameTargetSetup(project, root)).show()
        }.onFailure { Messages.showErrorDialog(project, it.message ?: "Cannot open game targets.", "Add Target") }
    }
}

internal class AddGameTargetDialog(private val game: Project, private val setup: GameTargetSetup) :
    DialogWrapper(game) {
    private val existing = setup.presets()
    private val contributed = GameTargetContribution.EP.extensionList.associateBy { it.label }
    private val platforms = listOf("iOS", "Android") + contributed.keys
    private var platform by mutableStateOf(platforms.first())
    private fun choices() = existing.filter { it.platform == platform }
    private var preset by mutableStateOf(choices().firstOrNull())
    private var name by mutableStateOf(preset?.name ?: platform)
    private var identifier by mutableStateOf(setup.defaultIdentifier())

    init {
        title = "Add Game Target"
        setOKButtonText("Add Target")
        init()
    }

    override fun createCenterPanel(): JComponent = composePanel(disposable) {
        Column {
            Label("Platform")
            ComboBox(platforms, platform, onSelectionChange = { value ->
                if (value != null && value != platform) {
                    platform = value
                    preset = choices().firstOrNull()
                    name = preset?.name ?: value
                }
            }) { Label(it) }
            val options = choices()
            if (options.isNotEmpty()) {
                Label("Godot export preset")
                val create = "Create new preset…"
                val entries = options.map { it.name }
                ComboBox(
                    entries.indices.toList() + entries.size,
                    preset?.let(
                        options::indexOf,
                    ) ?: entries.size,
                    onSelectionChange = {
                        preset = it?.let(options::getOrNull)
                        name = preset?.name.orEmpty()
                    },
                ) { Label(entries.getOrNull(it) ?: create) }
            }
            if (preset == null && platform !in contributed) {
                LabelledField("Preset name", name, { name = it })
                LabelledField("Application identifier", identifier, { identifier = it })
            }
            StatusLine(
                when (platform) {
                    "iOS" -> "Choose a simulator in the Run toolbar."
                    "Android" -> "Choose an Android device in the Run toolbar."
                    else -> "Continue to the platform setup."
                },
            )
        }
    }.apply { preferredSize = JBUI.size(440, 220) }

    override fun doOKAction() {
        runCatching {
            val external = contributed[platform]
            if (external == null) setup.add(platform, name.trim(), identifier.trim()) else external.configure(game)
        }.onSuccess {
            super.doOKAction()
        }.onFailure { setErrorText(it.message ?: "Cannot add target.") }
    }
}
