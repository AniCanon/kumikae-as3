package app.anicanon.kumikae.as3.project

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.settings.ExportPreset
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.actionSystem.ActionUpdateThread
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.progress.ProcessCanceledException
import com.intellij.openapi.progress.ProgressIndicator
import com.intellij.openapi.progress.Task
import com.intellij.openapi.project.DumbAwareAction
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.DialogWrapper
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import javax.swing.JComponent

class AddGameTargetAction : DumbAwareAction() {
    override fun getActionUpdateThread() = ActionUpdateThread.BGT
    private fun root(project: Project) = project.basePath?.let { LocalFileSystem.getInstance().findFileByPath(it) }
    override fun update(e: AnActionEvent) {
        val dir = e.project?.let(::root)
        e.presentation.isEnabledAndVisible = dir?.findChild("project.godot") != null &&
            dir.findChild("kas3.gdextension") != null
    }

    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val root = root(project) ?: return
        showAddGameTarget(project, root)
    }
}

internal fun showAddGameTarget(project: Project, root: VirtualFile, added: () -> Unit = {}) {
    val setup = GameTargetSetup(project, root)
    object : Task.Backgroundable(project, "Reading game targets") {
        private var presets = emptyList<ExportPreset>()
        private var identifier = ""

        override fun run(indicator: ProgressIndicator) {
            presets = setup.presets()
            identifier = setup.defaultIdentifier()
        }

        override fun onSuccess() {
            if (!project.isDisposed && AddGameTargetDialog(project, setup, presets, identifier).showAndGet()) added()
        }

        override fun onThrowable(error: Throwable) {
            Messages.showErrorDialog(project, error.message ?: "Cannot open game targets.", "Add Target")
        }
    }.queue()
}

internal class AddGameTargetDialog(
    private val game: Project,
    private val setup: GameTargetSetup,
    private val existing: List<ExportPreset>,
    defaultIdentifier: String,
) : DialogWrapper(game) {
    private val contributed = GameTargetContribution.EP.extensionList.associateBy { it.label }
    private val platforms = listOf("iOS", "Android", "Web") + contributed.keys
    private var platform by mutableStateOf(platforms.first())
    private fun choices() = existing.filter { it.platform == platform }
    private var preset by mutableStateOf(choices().firstOrNull())
    private var name by mutableStateOf(preset?.name ?: platform)
    private var identifier by mutableStateOf(defaultIdentifier)

    init {
        title = "Add Game Target"
        setOKButtonText("Add Target")
        init()
    }

    override fun createCenterPanel(): JComponent = composePanel(disposable) {
        SettingsSection("Target configuration") {
            Label("Platform")
            ComboBox(platforms, platform, onSelectionChange = { value ->
                if (value != null && value != platform) {
                    platform = value
                    preset = choices().firstOrNull()
                    name = preset?.name ?: value
                }
            }) { Label(it) }
            val options = choices()
            if (options.isNotEmpty() && platform !in contributed) {
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
                if (platform != "Web") LabelledField("Application identifier", identifier, { identifier = it })
            }
            StatusLine(
                when (platform) {
                    "iOS" -> "Run on an iOS simulator without signing in. Choose a device team in Export settings."
                    "Web" -> "Run exports the game and opens it in your browser."
                    "Android" -> "Run on an Android device without signing in. An upload key is used for Google Play."
                    else -> "Continue to the platform setup."
                },
            )
        }
    }.apply { minimumSize = JBUI.size(440, 190) }

    override fun doOKAction() {
        try {
            val external = contributed[platform]
            if (external == null) setup.add(platform, name.trim(), identifier.trim()) else external.configure(game)
            super.doOKAction()
        } catch (canceled: ProcessCanceledException) {
            throw canceled
        } catch (error: Exception) {
            setErrorText(error.message ?: "Cannot add target.")
        }
    }
}
