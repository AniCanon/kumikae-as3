package app.anicanon.kumikae.as3.scene

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

class SceneEditorProvider :
    FileEditorProvider,
    DumbAware {
    override fun accept(project: Project, file: VirtualFile) = file.extension == "tscn"
    override fun createEditor(project: Project, file: VirtualFile): FileEditor = SceneEditor(project, file)
    override fun getEditorTypeId() = "kumikae.scene"
    override fun getPolicy() = FileEditorPolicy.PLACE_BEFORE_DEFAULT_EDITOR
}

private class SceneEditor(private val project: Project, private val file: VirtualFile) :
    UserDataHolderBase(),
    FileEditor {
    private val controller = SceneController(IdeSceneRepository(project, file)).also { it.reload() }
    private val panel = composePanel(this) { SceneView(controller, ::choose) }
    private fun choose() {
        val root =
            generateSequence(file.parent) { it.parent }.firstOrNull { it.findChild("project.godot") != null } ?: return
        val descriptor = FileChooserDescriptorFactory.createSingleFileDescriptor(
            "as",
        ).withTitle("Attach AS3 Script").withRoots(root)
        FileChooser.chooseFile(descriptor, project, root) { chosen ->
            val relative = VfsUtilCore.getRelativePath(chosen, root, '/')
            if (relative != null) controller.attach("res://$relative")
        }
    }
    override fun getFile() = file
    override fun getComponent(): JComponent = panel
    override fun getPreferredFocusedComponent(): JComponent = panel
    override fun getName() = "Scene"
    override fun setState(state: FileEditorState) = Unit
    override fun isModified() = false
    override fun isValid() = file.isValid
    override fun addPropertyChangeListener(listener: PropertyChangeListener) = Unit
    override fun removePropertyChangeListener(listener: PropertyChangeListener) = Unit
    override fun dispose() = Unit
}
