package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.project.AddGameTargetDialog
import app.anicanon.kumikae.as3.project.GameTargetSetup
import app.anicanon.kumikae.as3.settings.ProjectSettingsEditorProvider
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.fileEditor.FileEditor
import com.intellij.openapi.project.DumbAwareAction
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.ProjectManager
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VfsUtil
import com.intellij.openapi.vfs.VirtualFile
import java.awt.Component
import java.awt.Container
import java.nio.file.Files
import java.util.function.Function
import java.util.function.Predicate
import javax.swing.Action
import javax.swing.JButton
import javax.swing.JComboBox
import javax.swing.JFrame

class UiProbeTargetMarker : Predicate<VirtualFile> {
    override fun test(file: VirtualFile): Boolean {
        check(!ApplicationManager.getApplication().isDispatchThread)
        return file.parent.findChild("probe-target.cfg") != null
    }
}

class UiProbeTargetSettingsAction : DumbAwareAction() {
    override fun actionPerformed(e: AnActionEvent) {
        UiProbeTargetSettings.opened = e.getData(CommonDataKeys.VIRTUAL_FILE)
    }
}

class UiProbeMissingDriver : Function<Project, List<Action>> {
    override fun apply(project: Project): List<Action> = throw UnsatisfiedLinkError("Missing example driver")
}

internal object UiProbeTargetSettings {
    var opened: VirtualFile? = null

    fun run() {
        val directory = Files.createTempDirectory("kas3-target-settings")
        val marker = directory.resolve("probe-target.cfg")
        val project = ProjectManager.getInstance().defaultProject
        lateinit var editor: FileEditor
        lateinit var frame: JFrame
        try {
            Files.writeString(directory.resolve("project.godot"), "config_version=5\n")
            Files.writeString(directory.resolve("export_presets.cfg"), "")
            Files.createFile(marker)
            val root = checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(directory))
            VfsUtil.markDirtyAndRefresh(false, true, true, root)
            val game = checkNotNull(root.findChild("project.godot"))
            UiProbeCompletion.edt {
                editor =
                    ProjectSettingsEditorProvider().createEditor(
                        project,
                        checkNotNull(root.findChild("export_presets.cfg")),
                    )
                frame = JFrame().apply {
                    contentPane.add(editor.component)
                    pack()
                }
                editor.selectNotify()
            }
            try {
                fun button() = children(editor.component).filterIsInstance<JButton>().singleOrNull {
                    it.text ==
                        "Probe target settings…"
                }
                UiProbeCompletion.await("initial configured target") { button() != null }
                UiProbeCompletion.edt { checkNotNull(button()).doClick() }
                UiProbeCompletion.await("settings action for the edited game") { opened == game }
                UiProbeCompletion.edt {
                    opened = null
                    val dialog = AddGameTargetDialog(project, GameTargetSetup(project, root), emptyList(), "")
                    val picker = children(dialog.contentPanel).filterIsInstance<JComboBox<*>>().single()
                    picker.selectedItem = "Probe target"
                    dialog.rootPane.defaultButton.doClick()
                }
                UiProbeCompletion.await("setup action for the edited game") { opened == game }
                Files.delete(marker)
                VfsUtil.markDirtyAndRefresh(false, true, true, root)
                check(root.findChild(marker.fileName.toString()) == null)
                UiProbeCompletion.await("removed target to leave the open editor") { button() == null }
                Files.createFile(marker)
                VfsUtil.markDirtyAndRefresh(false, true, true, root)
                checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(marker))
                UiProbeCompletion.await("added target to appear in the open editor") { button() != null }
                val group = DeviceCatalogContribution.EP.extensionList.single {
                    it.label == "Probe devices"
                }.read(project)
                check(group.devices.isEmpty() && group.emptyStatus == "Missing example driver")
            } finally {
                UiProbeCompletion.edt {
                    editor.deselectNotify()
                    Disposer.dispose(editor)
                    frame.dispose()
                    opened = null
                }
            }
        } finally {
            directory.toFile().deleteRecursively()
        }
    }

    private fun children(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(children(it)) }
    }
}
