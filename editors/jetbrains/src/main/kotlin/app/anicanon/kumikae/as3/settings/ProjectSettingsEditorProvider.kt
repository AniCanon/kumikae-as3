package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.project.AddGameTargetDialog
import app.anicanon.kumikae.as3.project.GameTargetSetup
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory
import com.intellij.openapi.fileEditor.FileEditor
import com.intellij.openapi.fileEditor.FileEditorPolicy
import com.intellij.openapi.fileEditor.FileEditorProvider
import com.intellij.openapi.fileEditor.FileEditorState
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.UserDataHolderBase
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.concurrency.AppExecutorUtil
import java.beans.PropertyChangeListener
import javax.swing.JComponent

class ProjectSettingsEditorProvider :
    FileEditorProvider,
    DumbAware {
    override fun accept(project: Project, file: VirtualFile) = file.name == "project.godot" ||
        (file.name == "export_presets.cfg" && file.parent?.findChild("project.godot") != null)
    override fun createEditor(project: Project, file: VirtualFile): FileEditor = ProjectSettingsEditor(project, file)
    override fun getEditorTypeId() = "kumikae.project.settings"
    override fun getPolicy() = FileEditorPolicy.PLACE_BEFORE_DEFAULT_EDITOR
}

private class ProjectSettingsEditor(private val project: Project, private val file: VirtualFile) :
    UserDataHolderBase(),
    FileEditor {
    private val gameFile = if (file.name ==
        "project.godot"
    ) {
        file
    } else {
        requireNotNull(file.parent.findChild("project.godot"))
    }
    private val controller = SettingsController(IdeSettingsRepository(project, gameFile, this)).also { it.reload() }
    private val panel = composePanel(this) {
        ProjectSettingsView(
            controller,
            ::choose,
            initialPage = if (file ==
                gameFile
            ) {
                "General"
            } else {
                "Export"
            },
            addTarget = {
                val setup = GameTargetSetup(project, gameFile.parent)
                if (app.anicanon.kumikae.as3.project.AddGameTargetDialog(
                        project,
                        setup,
                    ).showAndGet()
                ) {
                    controller.reload()
                }
            },
        )
    }

    override fun selectNotify() {
        if (!controller.dirty) controller.reload()
        ReadAction.nonBlocking<Map<String, String>> {
            val scenes = mutableMapOf<String, String>()
            VfsUtilCore.iterateChildrenRecursively(file.parent, { entry ->
                !entry.isDirectory || entry == file.parent ||
                    (
                        !entry.name.startsWith('.') && entry.findChild(".gdignore") == null &&
                            entry.findChild("project.godot") == null
                        )
            }) { entry ->
                if (!entry.isDirectory && GameResourceKind.Scene.accepts(entry.name)) {
                    val relative =
                        VfsUtilCore.getRelativePath(entry, file.parent, '/') ?: return@iterateChildrenRecursively true
                    scenes["res://$relative"] = relative
                    if (entry.extension == "tscn") {
                        val header = entry.inputStream.use {
                            it.readNBytes(512).toString(Charsets.UTF_8).substringBefore('\n')
                        }
                        Regex("""uid="(uid://[^"]+)"""").find(header)?.groupValues?.get(1)?.let {
                            scenes[it] = relative
                        }
                    }
                }
                true
            }
            scenes
        }.expireWith(this).finishOnUiThread(ModalityState.any()) { controller.sceneLabels = it }
            .submit(AppExecutorUtil.getAppExecutorService())
    }

    private fun choose(kind: GameResourceKind, selected: (String) -> Unit) {
        val descriptor = FileChooserDescriptorFactory.createSingleFileDescriptor().withTitle(
            kind.title,
        ).withRoots(file.parent).withFileFilter {
            kind.accepts(it.name) &&
                VfsUtilCore.isAncestor(file.parent, it, false)
        }
        FileChooser.chooseFile(descriptor, project, file.parent) { chosen ->
            val relative = VfsUtilCore.getRelativePath(chosen, file.parent, '/')
            if (relative != null) selected("res://$relative")
        }
    }

    override fun getFile() = file
    override fun getComponent(): JComponent = panel
    override fun getPreferredFocusedComponent(): JComponent = panel
    override fun getName() = "Settings"
    override fun setState(state: FileEditorState) = Unit
    override fun isModified() = controller.dirty
    override fun isValid() = file.isValid
    override fun addPropertyChangeListener(listener: PropertyChangeListener) = Unit
    override fun removePropertyChangeListener(listener: PropertyChangeListener) = Unit
    override fun dispose() = Unit
}
