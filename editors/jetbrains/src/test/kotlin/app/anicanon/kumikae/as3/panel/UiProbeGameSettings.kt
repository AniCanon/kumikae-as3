package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.settings.ArtworkButton
import app.anicanon.kumikae.as3.ui.TabControl
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ReadAction
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
            scene != null && scene.itemCount >= 1 &&
                (0 until scene.itemCount).none { scene.getItemAt(it).toString().startsWith("res://tests/") } &&
                panel.width > 0 && panel.isShowing &&
                children(panel).filterIsInstance<ArtworkButton>().let { tiles ->
                    tiles.count() == 2 && tiles.all { it.artwork != null }
                }
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
            val buttons = children(panel).filterIsInstance<ArtworkButton>().toList()
            val iconTile = buttons.single { it.splash == null }
            val snapshot = render(iconTile)
            if (System.getenv("KUMIKAE_ARTWORK_UI_ONLY") == "1") {
                val splash = buttons.single { it.splash != null }
                val artwork = checkNotNull(splash.artwork)
                check(artwork.width > artwork.height && artwork.image.width > artwork.image.height)
                check(artwork.width > 1280 && splash.splash?.width == 1200)
            }
            val gold = (0 until snapshot.width).sumOf { x ->
                (0 until snapshot.height).count { y ->
                    val rgb = snapshot.getRGB(x, y)
                    (rgb shr 16 and 255) > 232 && (rgb shr 8 and 255) in 155..220 && (rgb and 255) < 110
                }
            }
            check(gold > 120) { "Project artwork is missing its accent pixels: $gold pixels." }
        }
        if (System.getenv("KUMIKAE_ARTWORK_UI_ONLY") == "1") UiProbeArtworkPicker.run(panel, file, output)
        app.invokeAndWait {
            children(panel).filterIsInstance<TabControl>().single { it.name == "Display" }.doClick()
        }
        await { children(panel).filterIsInstance<JSpinner>().count() == 2 }
        app.invokeAndWait { capture(panel, output.resolve("godot-display.png")) }
        val tests = checkNotNull(root.findChild("tests"))
        ReadAction.run<RuntimeException> {
            check(ProjectFileIndex.getInstance(project).isInTestSourceContent(tests)) { "Unregistered game test root" }
        }
    }
    private fun children(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(children(it)) }
    }
    private fun capture(panel: JComponent, path: Path) {
        ImageIO.write(render(panel), "png", path.toFile())
    }
    private fun render(panel: JComponent): BufferedImage {
        val image = BufferedImage(panel.width, panel.height, BufferedImage.TYPE_INT_RGB)
        val g = image.createGraphics()
        try {
            panel.printAll(g)
        } finally {
            g.dispose()
        }
        return image
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
