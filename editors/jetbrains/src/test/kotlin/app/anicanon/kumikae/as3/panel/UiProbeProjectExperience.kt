package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.scene.SceneController
import app.anicanon.kumikae.as3.scene.SceneDocument
import app.anicanon.kumikae.as3.scene.SceneNode
import app.anicanon.kumikae.as3.scene.SceneRepository
import app.anicanon.kumikae.as3.scene.SceneView
import app.anicanon.kumikae.as3.settings.ProjectSettings
import app.anicanon.kumikae.as3.settings.ProjectSettingsView
import app.anicanon.kumikae.as3.settings.SettingsController
import app.anicanon.kumikae.as3.settings.SettingsRepository
import app.anicanon.kumikae.as3.ui.TabControl
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.util.Disposer
import java.awt.Component
import java.awt.Container
import java.awt.image.BufferedImage
import java.nio.file.Path
import javax.imageio.ImageIO
import javax.swing.JButton
import javax.swing.JComponent
import javax.swing.JFrame
import javax.swing.JTextField

internal object UiProbeProjectExperience {
    fun run(destination: Path, theme: String) {
        val owner = Disposer.newDisposable()
        val repository = UiProbeSettingsRepository()
        val controller = SettingsController(repository)
        val scenes = UiProbeSceneRepository()
        val sceneController = SceneController(scenes)
        lateinit var frame: JFrame
        lateinit var panel: JComponent
        edt {
            controller.reload()
            frame = JFrame()
            panel =
                composePanel(owner) { ProjectSettingsView(controller, { _, callback -> callback("res://icon.svg") }) }
            frame.contentPane.add(panel)
        }
        try {
            await { descendants(panel).filterIsInstance<JTextField>().count() == 3 }
            render(panel, destination.resolve("$theme-project-general.png"))
            edt { descendants(panel).filterIsInstance<JTextField>().first().text = "Edited game" }
            click(panel, "Apply")
            await { repository.value.name == "Edited game" }
            tab(panel, "Display")
            await { descendants(panel).filterIsInstance<JTextField>().count() == 2 }
            render(panel, destination.resolve("$theme-project-display.png"))
            edt { descendants(panel).filterIsInstance<JTextField>().first().text = "0" }
            click(panel, "Apply")
            await { controller.status.contains("between 1") }
            check(repository.value.width == "1152")
            render(panel, destination.resolve("$theme-project-invalid.png"))
            click(panel, "Reload")
            tab(panel, "Export")
            await { descendants(panel).filterIsInstance<JTextField>().count() == 2 }
            render(panel, destination.resolve("$theme-project-export.png"))
            edt {
                frame.contentPane.remove(panel)
                sceneController.reload()
                panel = composePanel(owner) { SceneView(sceneController, { sceneController.attach("res://App.as") }) }
                frame.contentPane.add(panel)
            }
            await { descendants(panel).filterIsInstance<JButton>().any { it.text == "Attach Script…" } }
            click(panel, "Attach Script…")
            await { sceneController.selected?.script == "res://App.as" }
            render(panel, destination.resolve("$theme-scene.png"))
            click(panel, "Open Script")
            check(scenes.opened == "res://App.as")
            click(panel, "Detach Script")
            await { sceneController.selected?.script == null }
        } finally {
            edt {
                Disposer.dispose(owner)
                frame.dispose()
            }
        }
    }

    private fun tab(panel: JComponent, name: String) = edt {
        descendants(panel).filterIsInstance<TabControl>().single { it.name == name }.doClick()
    }
    private fun click(panel: JComponent, text: String) = edt {
        descendants(panel).filterIsInstance<JButton>().single { it.text == text }.doClick()
    }
    private fun descendants(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(descendants(it)) }
    }
    private fun edt(work: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(work)
    private fun await(condition: () -> Boolean) {
        val deadline = System.nanoTime() + 10_000_000_000
        while (System.nanoTime() < deadline) {
            var passed = false
            edt { passed = condition() }
            if (passed) return
            Thread.sleep(30)
        }
        error("Project experience UI did not reach the expected state")
    }
    private fun render(panel: JComponent, path: Path) = edt {
        panel.setSize(960, 720)
        repeat(3) { layout(panel) }
        val image = BufferedImage(960, 720, BufferedImage.TYPE_INT_ARGB)
        val graphics = image.createGraphics()
        try {
            panel.printAll(graphics)
        } finally {
            graphics.dispose()
        }
        ImageIO.write(image, "png", path.toFile())
    }
    private fun layout(container: Container) {
        container.doLayout()
        container.components.filterIsInstance<Container>().forEach(::layout)
    }
}

internal class UiProbeSettingsRepository : SettingsRepository {
    var value = ProjectSettings.read(
        "[application]\nconfig/name=\"Support Sprint\"\nrun/main_scene=\"res://main.tscn\"\nconfig/icon=\"res://icon.svg\"\n",
        "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n[preset.0.options]\napplication/bundle_identifier=\"org.example.game\"\n",
    )
    override fun preview(path: String): BufferedImage = BufferedImage(80, 80, BufferedImage.TYPE_INT_ARGB).also {
        val graphics = it.createGraphics()
        try {
            graphics.color = java.awt.Color(245, 196, 65)
            graphics.fillRoundRect(0, 0, 80, 80, 16, 16)
            graphics.color = java.awt.Color(46, 67, 57)
            graphics.fillRect(20, 20, 16, 40)
            graphics.fillRect(36, 20, 24, 12)
            graphics.fillRect(36, 36, 18, 12)
        } finally {
            graphics.dispose()
        }
    }
    override fun load() = value
    override fun save(expected: ProjectSettings, draft: ProjectSettings): ProjectSettings {
        check(expected.projectText == value.projectText)
        val (main, export) = draft.write(value.projectText, value.exportText)
        value = ProjectSettings.read(main, export)
        return value
    }
}

internal class UiProbeSceneRepository : SceneRepository {
    var source = "[gd_scene format=3]\n[node name=\"Game\" type=\"Node2D\"]\n" +
        "[node name=\"Sound\" type=\"AudioStreamPlayer\" parent=\".\"]\n"
    var opened = ""
    override fun read() = source
    override fun attach(expected: String, node: SceneNode, path: String?): String {
        check(expected == source)
        source = SceneDocument(source).attach(node.path, path)
        return source
    }
    override fun open(path: String) {
        opened = path
    }
}
