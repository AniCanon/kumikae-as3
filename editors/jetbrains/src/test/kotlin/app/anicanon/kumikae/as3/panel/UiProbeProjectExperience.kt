package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.mutableStateOf
import app.anicanon.kumikae.as3.project.GodotInstallation
import app.anicanon.kumikae.as3.project.GodotSetup
import app.anicanon.kumikae.as3.project.GodotSetupController
import app.anicanon.kumikae.as3.project.GodotSetupSection
import app.anicanon.kumikae.as3.scene.SceneController
import app.anicanon.kumikae.as3.scene.SceneDocument
import app.anicanon.kumikae.as3.scene.SceneNode
import app.anicanon.kumikae.as3.scene.SceneRepository
import app.anicanon.kumikae.as3.scene.SceneView
import app.anicanon.kumikae.as3.settings.AndroidUploadKeys
import app.anicanon.kumikae.as3.settings.Artwork
import app.anicanon.kumikae.as3.settings.ExportPreset
import app.anicanon.kumikae.as3.settings.IosExecutionMode
import app.anicanon.kumikae.as3.settings.ProjectSettings
import app.anicanon.kumikae.as3.settings.ProjectSettingsView
import app.anicanon.kumikae.as3.settings.SettingsController
import app.anicanon.kumikae.as3.settings.SettingsPage
import app.anicanon.kumikae.as3.settings.SettingsRepository
import app.anicanon.kumikae.as3.settings.SigningTeam
import app.anicanon.kumikae.as3.settings.TesterAccount
import app.anicanon.kumikae.as3.settings.TesterAccounts
import app.anicanon.kumikae.as3.settings.TesterBuild
import app.anicanon.kumikae.as3.settings.TesterConnection
import app.anicanon.kumikae.as3.settings.TesterDeliveryDialog
import app.anicanon.kumikae.as3.settings.TesterGroup
import app.anicanon.kumikae.as3.settings.UploadKeyDialog
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.ProjectManager
import com.intellij.openapi.util.Disposer
import java.awt.Color
import java.awt.Component
import java.awt.Container
import java.awt.image.BufferedImage
import java.nio.file.Path
import javax.imageio.ImageIO
import javax.swing.JButton
import javax.swing.JComboBox
import javax.swing.JComponent
import javax.swing.JFrame
import javax.swing.JLabel
import javax.swing.JList
import javax.swing.JPasswordField
import javax.swing.JScrollPane
import javax.swing.JSpinner
import javax.swing.JTextField
import javax.swing.SwingUtilities

internal object UiProbeProjectExperience {
    fun godotSetup(destination: Path, theme: String) {
        val owner = Disposer.newDisposable()
        val engine = GodotInstallation(Path.of("/Applications/Godot.app/Contents/MacOS/Godot"), "4.7.2")
        val setup = object : GodotSetup {
            override fun detect(): GodotInstallation? = null
            override fun choose(path: Path) = engine.copy(executable = path)
            override fun install() = engine
        }
        val worker = BackgroundWorker("Godot setup probe").also { Disposer.register(owner, it) }
        val controller = GodotSetupController(setup, worker)
        lateinit var frame: JFrame
        lateinit var panel: JComponent
        edt {
            panel = composePanel(owner) {
                GodotSetupSection(controller, true) { controller.choose(Path.of("/Applications/Custom Godot")) }
            }
            frame = JFrame().apply { contentPane.add(panel) }
            controller.detect()
        }
        try {
            await { descendants(panel).filterIsInstance<JButton>().any { it.text == "Install Godot 4.7.2" } }
            render(panel, destination.resolve("$theme-godot-missing.png"), 420, 140)
            click(panel, "Install Godot 4.7.2")
            await { descendants(panel).filterIsInstance<JLabel>().any { it.text == "Godot 4.7.2 · Ready" } }
            render(panel, destination.resolve("$theme-godot-ready.png"), 420, 140)
            click(panel, "Choose existing…")
            await { controller.installation?.executable == Path.of("/Applications/Custom Godot") }
        } finally {
            edt {
                Disposer.dispose(owner)
                frame.dispose()
            }
        }
    }

    fun run(destination: Path, theme: String) {
        checkCatalog(destination, theme)
        checkUploadKeyDialog(destination, theme)
        checkTesterDialog(destination, theme)
        val owner = Disposer.newDisposable()
        val repository = UiProbeSettingsRepository()
        val controller = SettingsController(repository)
        var chosenResource = ""
        val configuredTarget = mutableStateOf(false)
        var targetSettingsOpened = false
        var distributed = ""
        var released = ""
        var portal = ""
        var keyPreset = ""
        val scenes = UiProbeSceneRepository()
        val sceneController = SceneController(scenes)
        lateinit var frame: JFrame
        lateinit var panel: JComponent
        edt {
            controller.reload()
            frame = JFrame()
            panel =
                composePanel(owner) {
                    ProjectSettingsView(
                        controller,
                        { kind, callback ->
                            chosenResource = kind.title
                            callback("res://new-icon.svg")
                        },
                        distribute = { _, preset -> distributed = preset.platform },
                        release = { _, preset -> released = preset.platform },
                        addTarget = {},
                        targetSettings = if (configuredTarget.value) {
                            mapOf("Example platform" to { targetSettingsOpened = true })
                        } else {
                            emptyMap()
                        },
                        export = { _, _ -> },
                        openStore = { portal = it },
                        configureKey = { keyPreset = it.name },
                    )
                }
            frame.contentPane.add(panel)
        }
        try {
            await { descendants(panel).filterIsInstance<JTextField>().count() == 2 }
            edt {
                descendants(panel).filterIsInstance<JButton>()
                    .single { it.accessibleContext.accessibleName == "Choose application icon" }.doClick()
            }
            await { chosenResource == "Application icon" && controller.draft?.icon == "res://new-icon.svg" }
            render(panel, destination.resolve("$theme-project-general.png"))
            edt { descendants(panel).filterIsInstance<JTextField>().first().text = "Edited game" }
            click(panel, "Apply")
            await { repository.value.name == "Edited game" }
            tab(panel, "Display")
            await { descendants(panel).filterIsInstance<JTextField>().count() == 2 }
            render(panel, destination.resolve("$theme-project-display.png"))
            edt {
                descendants(panel).filterIsInstance<JSpinner>()
                    .single { it.accessibleContext.accessibleName == "Viewport width" }.value = 1280
            }
            click(panel, "Apply")
            await { repository.value.width == "1280" }
            click(panel, "Reload")
            tab(panel, "Export")
            fun combos() = descendants(panel).filterIsInstance<JComboBox<*>>()
            await { combos().any { it.selectedItem == IosExecutionMode.Automatic } }
            fun targetPicker() = combos().single { it.selectedItem is ExportPreset }
            edt {
                check(
                    (0 until targetPicker().itemCount).none {
                        targetPicker().getItemAt(it).toString() ==
                            "Example platform"
                    },
                )
                configuredTarget.value = true
            }
            await {
                (0 until targetPicker().itemCount).any {
                    targetPicker().getItemAt(it).toString() ==
                        "Example platform"
                }
            }
            edt {
                check(descendants(panel).filterIsInstance<JButton>().none { it.text == "Example platform settings…" }) {
                    "Companion settings appeared while iOS was selected"
                }
                val picker = combos().single { it.selectedItem is ExportPreset }
                picker.selectedIndex = picker.itemCount - 1
            }
            click(panel, "Example platform settings…")
            check(targetSettingsOpened)
            edt {
                check(combos().none { it.selectedItem == IosExecutionMode.Automatic })
                check(descendants(panel).filterIsInstance<JButton>().none { it.text == "Export All…" })
                combos().single().selectedIndex = 0
            }
            await { combos().any { it.selectedItem == IosExecutionMode.Automatic } }
            edt {
                val team = combos().single { it.selectedItem.toString() == "Choose a team…" }
                team.selectedIndex =
                    (0 until team.itemCount).single { team.getItemAt(it).toString().contains("Personal") }
            }
            await {
                descendants(panel).filterIsInstance<JButton>().any {
                    it.text?.contains("prepare Xcode project", ignoreCase = true) == true && !it.isEnabled
                }
            }
            edt {
                val team = combos().single { it.selectedItem.toString().contains("Personal") }
                team.selectedIndex =
                    (0 until team.itemCount).single { team.getItemAt(it).toString().contains("Studio") }
            }
            await { controller.draft?.signingTeam == "EXAMPLE123" && controller.dirty }
            render(panel, destination.resolve("$theme-project-export.png"))
            edt {
                combos().single { it.selectedItem == IosExecutionMode.Automatic }.selectedItem =
                    IosExecutionMode.Native
            }
            click(panel, "Apply")
            await {
                repository.value.executionMode == IosExecutionMode.Native &&
                    repository.value.signingTeam == "EXAMPLE123"
            }

            click(panel, "Distribute to testers…")
            check(distributed == "iOS")
            click(panel, "Prepare Xcode project…")
            click(panel, "App Store Connect")
            check(released == "iOS" && portal == "iOS")
            edt {
                val target = combos().single { it.selectedItem is ExportPreset }
                target.selectedIndex = 1
            }
            click(panel, "Configure upload key…")
            click(panel, "Play Console")
            click(panel, "Distribute to testers…")
            check(distributed == "Android")
            click(panel, "Build Play bundle…")
            check(keyPreset == "Google Play" && released == "Android" && portal == "Android")
            render(panel, destination.resolve("$theme-project-android.png"))
            tab(panel, "Properties")
            await {
                descendants(panel).filterIsInstance<JLabel>().any {
                    it.text == "Key"
                }
            }
            render(panel, destination.resolve("$theme-project-properties.png"))
            tab(panel, "Export")
            for (width in listOf(960, 640, 420)) {
                edt {
                    panel.setSize(width, 720)
                    repeat(4) { layout(panel) }
                }
                await { fitsViewport(panel) }
            }
            render(panel, destination.resolve("$theme-project-narrow.png"), 420, 720)
            for (page in listOf(
                SettingsPage.General,
                SettingsPage.Display,
                SettingsPage.Properties,
                SettingsPage.Export,
            )) {
                edt {
                    val navigation = combos().single { it.selectedItem is SettingsPage }
                    navigation.selectedItem = page
                }
                await { combos().any { it.selectedItem == page } && fitsViewport(panel) }
            }

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

    private fun checkTesterDialog(destination: Path, theme: String) {
        val groups = listOf(TesterGroup("qa", "Quality assurance"), TesterGroup("studio", "Studio team"))
        var stored: TesterConnection? = null
        val store = object : TesterAccounts {
            override fun read() = stored
            override fun remember(connection: TesterConnection?) {
                stored = connection
            }
            override fun connect(account: TesterAccount) = TesterConnection(account, "Example Studio", groups)
        }
        lateinit var dialog: TesterDeliveryDialog
        edt {
            val project = ProjectManager.getInstance().defaultProject
            dialog =
                TesterDeliveryDialog(project, TesterBuild("iOS", "org.example.game", "1.2", "24", "EXAMPLE123"), store)
        }
        val panel = dialog.contentPanel
        try {
            fun fields() = descendants(panel).filterIsInstance<JTextField>().toList()
            await {
                fields().size == 3 &&
                    descendants(panel).filterIsInstance<JButton>().any { it.text == "Connect account" && it.isEnabled }
            }
            edt {
                fields()[0].text = destination.resolve("AuthKey_EXAMPLE123.p8").toString()
                fields()[1].text = "fixture-issuer"
                fields()[2].text = "EXAMPLE123"
            }
            render(panel, destination.resolve("$theme-connect-testflight.png"), 520, 500)
            click(panel, "Connect account")
            await { descendants(panel).filterIsInstance<JComboBox<*>>().any { it.selectedItem == groups[0] } }
            edt { descendants(panel).filterIsInstance<JComboBox<*>>().single().selectedIndex = 1 }
            render(panel, destination.resolve("$theme-review-testflight.png"), 520, 400)
            click(panel, "Distribute to testers")
            await { dialog.approved?.selected == "studio" && stored?.selected == "studio" }
        } finally {
            edt { if (dialog.approved == null) dialog.doCancelAction() }
        }
    }

    private fun checkUploadKeyDialog(destination: Path, theme: String) {
        lateinit var dialog: UploadKeyDialog
        edt {
            val project = ProjectManager.getInstance().defaultProject
            val store = AndroidUploadKeys(project, destination.toString(), "UI fixture")
            dialog = UploadKeyDialog(project, "Google Play", store)
        }
        try {
            val panel = dialog.contentPanel
            await { descendants(panel).filterIsInstance<JButton>().any { it.text == "Create new…" } }
            edt {
                val password = descendants(panel).filterIsInstance<JPasswordField>().single()
                check(password.echoChar != '\u0000' && password.accessibleContext.accessibleName == "Keystore password")
            }
            render(panel, destination.resolve("$theme-upload-key.png"), 500, 420)
        } finally {
            edt { dialog.doCancelAction() }
        }
    }

    private fun checkCatalog(destination: Path, theme: String) {
        val owner = Disposer.newDisposable()
        val running = SimulatorDevice("first", "iPhone 18 Pro", "iOS 27.0", "Booted")
        val stopped = SimulatorDevice("second", "iPad Air", "iPadOS 27.0", "Shutdown")
        val android = AndroidAvd("Pixel_9")
        var opened = ""
        var refreshes = 0
        val mode = mutableStateOf(AndroidGraphics.DEVICE)
        lateinit var panel: JComponent
        lateinit var frame: JFrame
        edt {
            panel = composePanel(owner) {
                DeviceCatalogView(
                    listOf(stopped, running), listOf(android), "", "", false, mode.value,
                    { opened = it.id }, { opened = it.name }, { mode.value = it }, { refreshes++ },
                )
            }
            frame = JFrame().apply { contentPane.add(panel) }
        }
        try {
            fun buttons() = descendants(panel).filterIsInstance<JButton>()
            fun button(name: String) = buttons().single { it.accessibleContext.accessibleName == name }
            await { buttons().any { it.accessibleContext.accessibleName == "Open iPhone 18 Pro" } }
            check(descendants(panel).none { it is JComboBox<*> })
            render(panel, destination.resolve("$theme-device-catalog.png"), 420, 340)
            edt {
                button("Open iPhone 18 Pro").doClick()
                check(opened == running.id)
                button("Start iPad Air").doClick()
                check(opened == stopped.id)
                button("Start Pixel 9").doClick()
                check(opened == android.name)
                button("Refresh devices").doClick()
                button("Emulator graphics").doClick()
            }
            await { descendants(panel).any { it is JComboBox<*> } }
            edt { descendants(panel).filterIsInstance<JComboBox<*>>().single().selectedItem = AndroidGraphics.SOFTWARE }
            await { mode.value == AndroidGraphics.SOFTWARE && refreshes == 1 }
            render(panel, destination.resolve("$theme-device-graphics.png"), 300, 390)
        } finally {
            edt {
                Disposer.dispose(owner)
                frame.dispose()
            }
        }
    }

    private fun fitsViewport(panel: JComponent): Boolean {
        repeat(4) { layout(panel) }
        val scroll = descendants(panel).filterIsInstance<JScrollPane>().single()
        val view = scroll.viewport.view
        val width = scroll.viewport.extentSize.width
        if (scroll.horizontalScrollBar.isVisible || view.width != width) return false
        return descendants(view).filter {
            (it is JButton && !it.text.isNullOrBlank()) || it is JComboBox<*> || it is JTextField
        }.all {
            val rect = SwingUtilities.convertRectangle(it.parent, it.bounds, view)
            rect.x >= 0 && rect.x + rect.width <= width && it.width > 0 &&
                it.y >= 0 && it.y + it.height <= it.parent.height
        }
    }

    private fun tab(panel: JComponent, name: String) = edt {
        val list = descendants(panel).filterIsInstance<JList<*>>().single { it.name == "Project settings navigation" }
        list.selectedIndex = SettingsPage.valueOf(name).ordinal
    }
    private fun click(panel: JComponent, text: String) {
        fun button() = descendants(panel).filterIsInstance<JButton>().singleOrNull { it.text == text }
        await { button()?.isEnabled == true }
        edt { checkNotNull(button()).doClick() }
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
    private fun render(panel: JComponent, path: Path, width: Int = 960, height: Int = 720) = edt {
        panel.setSize(width, height)
        repeat(3) { layout(panel) }
        descendants(panel).filterIsInstance<JScrollPane>().forEach { it.verticalScrollBar.value = 0 }
        val image = BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB)
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
    override val hasXcodeTeams = true
    override fun loadTeams(ready: (Result<List<SigningTeam>>) -> Unit) = ready(
        Result.success(
            listOf(SigningTeam("EXAMPLE123", "Example Studio"), SigningTeam("PERSONAL12", "Individual", true)),
        ),
    )

    var value = ProjectSettings.read(
        "[application]\nconfig/name=\"Example Game\"\nrun/main_scene=\"res://main.tscn\"\nconfig/icon=\"res://icon.svg\"\n",
        "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n" +
            "[preset.0.options]\napplication/bundle_identifier=\"org.example.game\"\n" +
            "[preset.1]\nname=\"Google Play\"\nplatform=\"Android\"\n" +
            "[preset.1.options]\npackage/unique_name=\"org.example.game\"\n",
    )
    override fun preview(path: String): Artwork = BufferedImage(80, 80, BufferedImage.TYPE_INT_ARGB).also {
        val graphics = it.createGraphics()
        try {
            graphics.color = Color(245, 196, 65)
            graphics.fillRoundRect(0, 0, 80, 80, 16, 16)
            graphics.color = Color(46, 67, 57)
            graphics.fillRect(20, 20, 16, 40)
            graphics.fillRect(36, 20, 24, 12)
            graphics.fillRect(36, 36, 18, 12)
        } finally {
            graphics.dispose()
        }
    }.let(::Artwork)
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
