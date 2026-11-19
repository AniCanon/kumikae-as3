package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.project.GamePane
import com.intellij.ide.impl.OpenProjectTask
import com.intellij.ide.impl.ProjectUtil
import com.intellij.ide.impl.TrustedPathsSettings
import com.intellij.ide.projectView.ProjectView
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.ex.ProjectManagerEx
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.wm.ToolWindowManager
import com.intellij.psi.PsiManager
import com.intellij.util.ui.tree.TreeUtil
import java.awt.event.MouseEvent
import java.nio.file.Files
import javax.swing.JTree
import javax.swing.tree.TreePath

/** Dispatches mouse events through Rider's project tree with an expandable AS3 file. */
internal object UiProbeGameTree {
    fun run() {
        val root = Files.createTempDirectory("kas3-game-tree")
        val trust = TrustedPathsSettings.getInstance()
        val original = trust.getTrustedPaths().toList()
        try {
            Files.writeString(root.resolve("project.godot"), "config_version=5\n")
            val source = root.resolve("Score.as")
            Files.writeString(source, "package { public class Score { public var n:int; } }")
            trust.addTrustedPath(root.toString())
            val project = checkNotNull(ProjectUtil.openOrImport(root, OpenProjectTask { forceOpenInNewFrame = true }))
            try {
                val file = checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(source))
                val view = ProjectView.getInstance(project)
                val files = FileEditorManager.getInstance(project)
                UiProbeCompletion.edt {
                    val windows = ToolWindowManager.getInstance(project)
                    val devices = checkNotNull(windows.getToolWindow("Game Devices"))
                    check(devices.stripeTitle == "Devices") { "Device window was labelled ${devices.stripeTitle}" }
                    checkNotNull(windows.getToolWindow("Project")).show()
                    view.changeView(GamePane.ID)
                }
                UiProbeCompletion.await("Game pane") {
                    view.currentViewId == GamePane.ID && view.currentProjectViewPane?.tree?.isShowing == true
                }
                UiProbeCompletion.edt { view.select(PsiManager.getInstance(project).findFile(file), file, false) }
                lateinit var tree: JTree
                lateinit var path: TreePath
                UiProbeCompletion.await("Score.as in the Game tree") {
                    tree = checkNotNull(view.currentProjectViewPane.tree)
                    val selected = tree.selectionPath
                    if (selected == null || selected.lastPathComponent.toString() != "Score.as") return@await false
                    path = selected
                    true
                }
                UiProbeCompletion.edt {
                    tree.collapsePath(path)
                    files.closeFile(file)
                }
                UiProbeCompletion.await("closed Score.as") { !files.isFileOpen(file) }
                UiProbeCompletion.edt { click(tree, path, disclosure = true) }
                UiProbeCompletion.await("class beneath Score.as") {
                    tree.isExpanded(path) && tree.getPathForRow(tree.getRowForPath(path) + 1)?.parentPath == path
                }
                UiProbeCompletion.edt {
                    check(!files.isFileOpen(file)) { "The disclosure arrow opened the file" }
                    click(tree, path, disclosure = true)
                    check(!tree.isExpanded(path)) { "The disclosure arrow did not collapse the file" }
                    click(tree, path, disclosure = false)
                }
                UiProbeCompletion.await("double-click to open Score.as") {
                    check(!tree.isExpanded(path)) { "Double-clicking the file name expanded its members" }
                    files.selectedFiles.contains(file)
                }
                UiProbeCompletion.edt { click(tree, path, disclosure = true) }
                UiProbeCompletion.await("expanded file") { tree.isExpanded(path) }
                UiProbeCompletion.edt {
                    click(tree, path, disclosure = false)
                    check(tree.isExpanded(path)) { "Double-clicking the file name collapsed its members" }
                }
            } finally {
                UiProbeCompletion.edt { ProjectManagerEx.getInstanceEx().forceCloseProject(project, false) }
            }
        } finally {
            trust.setTrustedPaths(original)
            root.toFile().deleteRecursively()
        }
    }

    private fun click(tree: JTree, path: TreePath, disclosure: Boolean) {
        val bounds = checkNotNull(tree.getPathBounds(path))
        val y = bounds.y + bounds.height / 2
        val x = if (disclosure) {
            (0 until bounds.x).first { TreeUtil.isLocationInExpandControl(tree, path, it, y) }
        } else {
            bounds.x + bounds.width / 2
        }
        val start = System.currentTimeMillis()
        for (count in 1..if (disclosure) 1 else 2) {
            for (event in listOf(MouseEvent.MOUSE_PRESSED, MouseEvent.MOUSE_RELEASED, MouseEvent.MOUSE_CLICKED)) {
                val mouse = MouseEvent(tree, event, start + count * 50, 0, x, y, count, false, MouseEvent.BUTTON1)
                tree.dispatchEvent(mouse)
            }
        }
    }
}
