package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.settings.ArtworkGallery
import app.anicanon.kumikae.as3.settings.ArtworkPickerDialog
import app.anicanon.kumikae.as3.settings.GameArtwork
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.ui.DialogWrapper
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.ui.SearchTextField
import java.awt.Component
import java.awt.Container
import java.awt.Window
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import javax.imageio.ImageIO
import javax.swing.JButton
import javax.swing.JComponent
import javax.swing.JDialog

internal object UiProbeArtworkPicker {
    fun run(settings: JComponent, file: VirtualFile, output: Path) {
        val app = ApplicationManager.getApplication()
        fun source() = checkNotNull(FileDocumentManager.getInstance().getDocument(file)).text
        var original = ""
        edt { original = source() }
        val game = file.parent.toNioPath()
        checkVectorPreview(game.parent)
        val external = Files.createTempFile(game.parent, "title-art-", ".png")
        try {
            Files.copy(
                game.resolve("assets/boot-splash.png"),
                external,
                StandardCopyOption.REPLACE_EXISTING,
            )
            val picked = checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(external))
            for (pass in 0..2) {
                val splash = pass != 0
                val role = if (splash) "boot splash" else "application icon"
                val use = if (splash) "Use Splash" else "Use Icon"
                val existing = if (splash) "boot-splash.png" else "app-icon.svg"
                val testName = if (splash) "boot-splash-picker" else "icon-picker"
                app.invokeLater({
                    children(settings).filterIsInstance<JButton>()
                        .single { it.accessibleContext.accessibleName == "Choose $role" }.doClick()
                }, ModalityState.any())
                fun window() = Window.getWindows().filterIsInstance<JDialog>().lastOrNull {
                    it.isShowing && it.title == "Choose $role"
                }
                fun gallery() = children(checkNotNull(window())).filterIsInstance<ArtworkGallery>().single()
                fun button(name: String) = children(checkNotNull(window())).filterIsInstance<JButton>().single {
                    it.text == name
                }
                fun search(text: String) {
                    children(checkNotNull(window())).filterIsInstance<SearchTextField>().single().text = text
                }
                try {
                    await(testName) {
                        window() != null && gallery().selectedValue?.endsWith("/$existing") == true &&
                            button(use).isEnabled && gallery().missingVisible().isEmpty()
                    }
                    edt {
                        val list = gallery()
                        if (splash) {
                            check(
                                (0 until list.model.size).all {
                                    list.model.getElementAt(it).endsWith(".png", true)
                                },
                            )
                        }
                        val root = checkNotNull(window()).rootPane
                        val image = BufferedImage(root.width, root.height, BufferedImage.TYPE_INT_RGB)
                        val g = image.createGraphics()
                        try {
                            root.printAll(g)
                        } finally {
                            g.dispose()
                        }
                        ImageIO.write(image, "png", output.resolve("$testName.png").toFile())
                        search("no-artwork-with-this-name")
                    }
                    await("empty filtered selection") { gallery().model.size == 0 && !button(use).isEnabled }
                    edt { search("assets app-icon.png") }
                    await("PNG preview") { gallery().model.size == 1 && button(use).isEnabled }
                    if (splash) {
                        edt {
                            val dialog = DialogWrapper.findInstance(checkNotNull(window()))
                                as ArtworkPickerDialog
                            dialog.stage(picked)
                        }
                        await("external image preview") {
                            gallery().selectedValue == picked.url && button("Import and Use").isEnabled
                        }
                        check(!Files.exists(game.resolve("assets").resolve(external.fileName)))
                    }
                    val action = when (pass) {
                        1 -> "Cancel"
                        2 -> "Import and Use"
                        else -> use
                    }
                    edt { button(action).doClick() }
                    await("close artwork picker") { window() == null }
                } finally {
                    edt { window()?.let { button("Cancel").doClick() } }
                }
                edt { check(source() == original) { "Choosing an image wrote project.godot before Apply" } }
            }
            fun settingsButton(name: String) = children(settings).filterIsInstance<JButton>().single { it.text == name }
            await("apply selected icon") { settingsButton("Apply").isEnabled }
            edt { settingsButton("Apply").doClick() }
            await("Godot icon and imported splash saved") {
                source() ==
                    original.replace(
                        "config/icon=\"res://assets/app-icon.svg\"",
                        "config/icon=\"res://assets/app-icon.png\"",
                    ).replace("res://assets/boot-splash.png", "res://assets/${external.fileName}")
            }
            check(Files.mismatch(external, game.resolve("assets").resolve(external.fileName)) == -1L)
        } finally {
            Files.deleteIfExists(external)
        }
    }

    private fun checkVectorPreview(folder: Path) {
        val path = Files.createTempFile(folder, "wide-art-", ".svg")
        try {
            Files.writeString(
                path,
                """<!DOCTYPE svg>
                <svg xmlns="http://www.w3.org/2000/svg" width="16000" height="2000" viewBox="0 0 8 1">
                <rect width="8" height="1" fill="#dda500"/></svg>
                """.trimIndent(),
            )
            val svg =
                checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(path))
            val art = checkNotNull(GameArtwork.decode(svg, 180, true))
            check(
                art.width == 16000 && art.height == 2000 && art.image.width in 179..181 && art.image.height in 21..24,
            ) {
                "Thumbnail measured ${art.image.width}×${art.image.height} for ${art.width}×${art.height} SVG"
            }
            check(art.image.getRGB(90, 10) and 0xffffff == 0xdda500)
        } finally {
            Files.deleteIfExists(path)
        }
    }

    private fun children(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(children(it)) }
    }
    private fun edt(block: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(block, ModalityState.any())
    private fun await(label: String, ready: () -> Boolean) {
        val until = System.nanoTime() + 20_000_000_000L
        while (System.nanoTime() < until) {
            var done = false
            edt { done = ready() }
            if (done) return
            Thread.sleep(50)
        }
        error("Timed out waiting for $label")
    }
}
