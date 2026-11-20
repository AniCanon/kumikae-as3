package app.anicanon.kumikae.as3.project

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.language.CompilerLocator
import app.anicanon.kumikae.as3.panel.BackgroundWorker
import app.anicanon.kumikae.as3.run.IdeGodot
import app.anicanon.kumikae.as3.run.KumikaeRunType
import app.anicanon.kumikae.as3.run.ensureDefaultTests
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.execution.RunManager
import com.intellij.ide.util.PropertiesComponent
import com.intellij.ide.util.projectWizard.WizardContext
import com.intellij.ide.wizard.AbstractNewProjectWizardStep
import com.intellij.ide.wizard.GeneratorNewProjectWizard
import com.intellij.ide.wizard.NewProjectWizardBaseStep
import com.intellij.ide.wizard.NewProjectWizardStep
import com.intellij.ide.wizard.RootNewProjectWizardStep
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptor
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.DumbService
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.ValidationInfo
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.util.SystemInfo
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.ui.dsl.builder.AlignX
import com.intellij.ui.dsl.builder.Panel
import org.jetbrains.compose.swing.components.button.CheckBox
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.interaction.enabled
import java.nio.file.Path

/** File > New Project > "Kumikae AS3 Game": a small game that runs, with a run target for each platform chosen. */
class As3ProjectWizard : GeneratorNewProjectWizard {
    override val id = "kumikae.as3.game"
    override val name = "Kumikae AS3 Game"
    override val icon = PluginFiles.mark
    override val description = "A Godot game written in ActionScript 3"

    override fun createStep(context: WizardContext): NewProjectWizardStep =
        PlatformStep(NewProjectWizardBaseStep(RootNewProjectWizardStep(context)))

    private class PlatformStep(private val base: NewProjectWizardBaseStep) : AbstractNewProjectWizardStep(base) {
        private val choices = Platform.choices()
        private val contributions = GameTargetContribution.EP.extensionList
        private val optional = mutableStateMapOf<String, Boolean>()
        private val chosen = mutableStateMapOf<Platform, Boolean>().apply {
            choices.forEach { put(it.platform, it.always) }
        }
        private var version by mutableStateOf("")
        private var bundle by mutableStateOf("")
        private var projectName by mutableStateOf(base.name)

        private val canInstall = SystemInfo.isMac && System.getProperty("os.arch") in setOf("aarch64", "arm64")
        private val engine = GodotSetupController(
            LocalGodotSetup({ IdeGodot.candidates() }, IdeGodot.installDirectory),
            BackgroundWorker("Godot wizard setup").also { Disposer.register(context.disposable, it) },
        )

        override fun setupUI(builder: Panel) {
            base.setupUI(builder)
            engine.detect()
            base.nameProperty.afterChange(context.disposable) { projectName = it }
            builder.row {
                cell(
                    composePanel(context.disposable) {
                        Column {
                            GodotSetupSection(engine, canInstall) {
                                val chooser = FileChooserDescriptor(true, true, false, false, false, false)
                                    .withTitle("Choose Godot App or Executable")
                                FileChooser.chooseFile(chooser, context.project, null) { engine.choose(it.toNioPath()) }
                            }
                            SettingsSection("Application") {
                                LabelledField(
                                    "Application identifier",
                                    bundle,
                                    { bundle = it },
                                    placeholder = ProjectTemplate.defaultBundle(projectName),
                                )
                                LabelledField("Version", version, {
                                    version = it
                                }, placeholder = ProjectTemplate.DEFAULT_VERSION)
                            }
                            SettingsSection("Run targets") {
                                for (choice in choices) {
                                    CheckBox(
                                        choice.platform.label,
                                        chosen[choice.platform] == true,
                                        { on: Boolean -> if (!choice.always) chosen[choice.platform] = on },
                                        SwingModifier.enabled(choice.available && !choice.always),
                                    )
                                    StatusLine(choice.note)
                                }
                                for (target in contributions) {
                                    CheckBox(target.label, optional[target.setupAction] == true, { selected: Boolean ->
                                        optional[target.setupAction] = selected
                                    })
                                }
                            }
                        }
                    },
                ).align(AlignX.FILL).validationOnApply {
                    val trimmedBundle = bundle.trim()
                    if (engine.busy) {
                        ValidationInfo("Wait for Godot setup to finish.")
                    } else if (version.isNotBlank() && !ProjectTemplate.VERSION_PATTERN.matches(version.trim())) {
                        ValidationInfo("Enter a version such as 1.0.0.")
                    } else if (trimmedBundle.isNotEmpty() && (
                            !trimmedBundle.contains('.') ||
                                !trimmedBundle.replace('-', '_').split('.').all(As3Names::valid)
                            )
                    ) {
                        ValidationInfo("Use a dot-separated ActionScript package name, such as com.studio.game.")
                    } else {
                        val invalidPlatform = trimmedBundle.takeIf { it.isNotEmpty() }?.let { id ->
                            chosen.filterValues { it }.keys.firstOrNull {
                                it != Platform.DESKTOP && !GameTargets.validIdentifier(it.label, id)
                            }
                        }
                        when (invalidPlatform) {
                            null -> null

                            Platform.ANDROID -> ValidationInfo(
                                "Android application identifiers cannot contain hyphens; use letters, digits and dots.",
                            )

                            else -> ValidationInfo("A package name is words joined by dots, like com.studio.game.")
                        }
                    }
                }
            }
        }

        override fun setupProject(project: Project) {
            engine.installation?.let { IdeGodot.remember(it.executable) }
            val folder = Path.of(base.path, base.name)
            val platforms = chosen.filterValues { it }.keys
            val template =
                ProjectTemplate(
                    base.name,
                    platforms,
                    bundle,
                    version.trim().ifEmpty {
                        ProjectTemplate.DEFAULT_VERSION
                    },
                )
            val compiler = CompilerLocator.find(
                folder,
                PropertiesComponent.getInstance().getValue(COMPILER_PATH).orEmpty(),
                System.getenv("PATH").orEmpty(),
                Path.of(System.getProperty("user.home")),
                PluginFiles.native(),
            )
            // A build of the compiler's own repository is linked, so that a rebuild reaches the game. Otherwise
            // the library the plugin carries is copied in.
            val built = ProjectTemplate.libraryBeside(compiler)
            val carried = PluginFiles.native().takeIf(ProjectTemplate::hasLibrary)
            template.write(folder, built ?: carried, link = built != null)
            LocalFileSystem.getInstance().refreshAndFindFileByNioFile(folder)?.refresh(true, true)
            addRunTargets(project, platforms)
            DumbService.getInstance(project).smartInvokeLater {
                val game = LocalFileSystem.getInstance().refreshAndFindFileByNioFile(folder.resolve("project.godot"))
                if (game != null) FileEditorManager.getInstance(project).openFile(game, true)
            }
            contributions.filter { optional[it.setupAction] == true }.forEach { it.configure(project) }
        }

        private fun addRunTargets(project: Project, platforms: Set<Platform>) {
            val manager = RunManager.getInstance(project)
            val game = manager.createConfiguration("Game", KumikaeRunType.factory(KumikaeRunType.Kind.GAME))
            manager.addConfiguration(game)
            ensureDefaultTests(manager)
            manager.selectedConfiguration = game
            if (Platform.WEB in platforms) GameTargetSetup.selectWeb(project, "Web")
            if (Platform.ANDROID in platforms) GameTargetSetup.selectAndroid(project, "Android")
            if (Platform.IOS in platforms) GameTargetSetup.selectSimulator(project, "iOS")
        }
    }

    private companion object {
        const val COMPILER_PATH = "kumikae.as3.compilerPath"
    }
}
