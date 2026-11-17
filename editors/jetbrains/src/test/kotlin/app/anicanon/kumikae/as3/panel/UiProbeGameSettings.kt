package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.ui.TabControl
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.roots.ProjectFileIndex
import com.intellij.openapi.vfs.VirtualFile
import java.awt.Component
import java.awt.Container
import java.awt.image.BufferedImage
import java.nio.file.Path
import javax.imageio.ImageIO
import javax.swing.JComboBox
import javax.swing.JComponent
import javax.swing.JLabel
import javax.swing.JSpinner

internal object UiProbeGameSettings {
    fun run(project: Project, root: VirtualFile, output: Path) {
        val file = checkNotNull(root.findChild("project.godot"))
        val manager = FileEditorManager.getInstance(project)
        val app = ApplicationManager.getApplication()
        lateinit var panel: JComponent
        app.invokeAndWait {
            manager.openFile(file, true)
            val selected = checkNotNull(manager.getSelectedEditor(file))
            check(selected.name == "Settings") { "Default editor: ${selected.name}" }
            panel = selected.component
        }
        await {
            if (file !in manager.selectedFiles) manager.openFile(file, true)
            val scene = children(panel).filterIsInstance<JComboBox<*>>().firstOrNull()
            scene != null && scene.itemCount > 1 && panel.width > 0 && panel.isShowing &&
                children(panel).filterIsInstance<JLabel>().none { it.text == "Preview unavailable" }
        }
        app.invokeAndWait {
            check(
                children(panel).filterIsInstance<JLabel>().none {
                    it.text?.contains("Preview unavailable") == true
                },
            ) {
                "The project icon did not load"
            }
            capture(panel, output.resolve("godot-settings.png"))
            val snapshot = ImageIO.read(output.resolve("godot-settings.png").toFile())
            val expected = 0xffcc59
            val gold = (0 until snapshot.width).sumOf { x ->
                (0 until snapshot.height).count { y -> snapshot.getRGB(x, y) and 0xffffff == expected }
            }
            check(gold > 120) { "Support Sprint artwork is missing its gold token: $gold pixels." }
            children(panel).filterIsInstance<TabControl>().single { it.name == "Display" }.doClick()
        }
        await { children(panel).filterIsInstance<JSpinner>().count() == 2 }
        app.invokeAndWait { capture(panel, output.resolve("godot-display.png")) }
        val tests = checkNotNull(root.findChild("tests"))
        com.intellij.openapi.application.ReadAction.run<RuntimeException> {
            check(ProjectFileIndex.getInstance(project).isInTestSourceContent(tests)) { "Unregistered game test root" }
        }
    }
    private fun children(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(children(it)) }
    }
    private fun capture(panel: JComponent, path: Path) {
        val image = BufferedImage(panel.width, panel.height, BufferedImage.TYPE_INT_RGB)
        val g = image.createGraphics()
        try {
            panel.printAll(g)
        } finally {
            g.dispose()
        }
        ImageIO.write(image, "png", path.toFile())
    }
    private fun await(condition: () -> Boolean) {
        val limit = System.nanoTime() + 20_000_000_000L
        while (System.nanoTime() < limit) {
            var ready = false
            ApplicationManager.getApplication().invokeAndWait { ready = condition() }
            if (ready) return
            Thread.sleep(100)
        }
        error("Godot settings did not display the project's scene choices or viewport controls")
    }
}
