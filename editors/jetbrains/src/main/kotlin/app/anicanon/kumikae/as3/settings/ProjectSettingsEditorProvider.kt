package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.ui.composePanel
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
import java.beans.PropertyChangeListener
import javax.swing.JComponent

class ProjectSettingsEditorProvider :
    FileEditorProvider,
    DumbAware {
    override fun accept(project: Project, file: VirtualFile) = file.name == "project.godot"
    override fun createEditor(project: Project, file: VirtualFile): FileEditor = ProjectSettingsEditor(project, file)
    override fun getEditorTypeId() = "kumikae.project.settings"
    override fun getPolicy() = FileEditorPolicy.PLACE_BEFORE_DEFAULT_EDITOR
}

private class ProjectSettingsEditor(private val project: Project, private val file: VirtualFile) :
    UserDataHolderBase(),
    FileEditor {
    private val controller = SettingsController(IdeSettingsRepository(project, file)).also { it.reload() }
    private val panel = composePanel(this) { ProjectSettingsView(controller, ::choose) }

    private fun choose(label: String, selected: (String) -> Unit) {
        val descriptor = FileChooserDescriptorFactory.createSingleFileDescriptor().withTitle(
            label,
        ).withRoots(file.parent)
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
