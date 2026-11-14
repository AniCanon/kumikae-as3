package app.anicanon.kumikae.as3.project

import androidx.compose.runtime.mutableStateMapOf
import app.anicanon.kumikae.as3.language.CompilerLocator
import app.anicanon.kumikae.as3.run.GodotConfigurationType
import app.anicanon.kumikae.as3.run.SimulatorConfigurationType
import app.anicanon.kumikae.as3.run.SimulatorRunConfiguration
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.execution.RunManager
import com.intellij.execution.configurations.ConfigurationType
import com.intellij.icons.AllIcons
import com.intellij.ide.plugins.cl.PluginAwareClassLoader
import com.intellij.ide.util.PropertiesComponent
import com.intellij.ide.util.projectWizard.WizardContext
import com.intellij.ide.wizard.AbstractNewProjectWizardStep
import com.intellij.ide.wizard.GeneratorNewProjectWizard
import com.intellij.ide.wizard.NewProjectWizardBaseStep
import com.intellij.ide.wizard.NewProjectWizardStep
import com.intellij.ide.wizard.RootNewProjectWizardStep
import com.intellij.openapi.extensions.PluginId
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.ui.dsl.builder.AlignX
import com.intellij.ui.dsl.builder.Panel
import org.jetbrains.compose.swing.components.button.CheckBox
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.interaction.enabled
import java.nio.file.Path

/** File > New Project > "AS3 Godot Game": a small game that runs, with a run target for each platform chosen. */
class As3ProjectWizard : GeneratorNewProjectWizard {
    override val id = "kumikae.as3.game"
    override val name = "AS3 Godot Game"
    override val icon = AllIcons.Actions.Execute
    override val description = "A Godot game written in ActionScript 3"

    override fun createStep(context: WizardContext): NewProjectWizardStep =
        PlatformStep(NewProjectWizardBaseStep(RootNewProjectWizardStep(context)))

    private class PlatformStep(private val base: NewProjectWizardBaseStep) : AbstractNewProjectWizardStep(base) {
        private val choices = Platform.choices(consolePlugin = consoleType() != null)
        private val chosen = mutableStateMapOf<Platform, Boolean>().apply {
            choices.forEach { put(it.platform, it.always) }
        }

        override fun setupUI(builder: Panel) {
            base.setupUI(builder)
            builder.row {
                cell(
                    composePanel(context.disposable) {
                        Column {
                            StatusLine("Where the game should run. Each choice adds its run target.")
                            for (choice in choices) {
                                CheckBox(
                                    choice.platform.label,
                                    chosen[choice.platform] == true,
                                    { on: Boolean -> if (!choice.always) chosen[choice.platform] = on },
                                    SwingModifier.enabled(choice.available && !choice.always),
                                )
                                StatusLine(choice.note)
                            }
                        }
                    },
                ).align(AlignX.FILL)
            }
        }

        override fun setupProject(project: Project) {
            val folder = Path.of(base.path, base.name)
            val platforms = chosen.filterValues { it }.keys
            val template = ProjectTemplate(base.name, platforms)
            val compiler = CompilerLocator.find(
                folder,
                PropertiesComponent.getInstance().getValue(COMPILER_PATH).orEmpty(),
                System.getenv("PATH").orEmpty(),
                Path.of(System.getProperty("user.home")),
            )
            template.write(folder, ProjectTemplate.libraryBeside(compiler))
            LocalFileSystem.getInstance().refreshAndFindFileByNioFile(folder)?.refresh(true, true)
            addRunTargets(project, platforms, template.bundle)
        }

        private fun addRunTargets(project: Project, platforms: Set<Platform>, bundle: String) {
            val manager = RunManager.getInstance(project)
            val game = manager.createConfiguration("Game", GodotConfigurationType().configurationFactories.first())
            manager.addConfiguration(game)
            manager.selectedConfiguration = game
            if (Platform.IOS in platforms) {
                val simulator = manager.createConfiguration(
                    "iOS Simulator",
                    SimulatorConfigurationType().configurationFactories.first(),
                )
                (simulator.configuration as SimulatorRunConfiguration).options.bundle = bundle
                manager.addConfiguration(simulator)
            }
            if (Platform.CONSOLE in platforms) {
                consoleType()?.configurationFactories?.firstOrNull()?.let { factory ->
                    manager.addConfiguration(manager.createConfiguration("Nintendo Switch", factory))
                }
            }
        }
    }

    private companion object {
        const val COMPILER_PATH = "kumikae.as3.compilerPath"
        const val CONSOLE_PLUGIN = "app.anicanon.kumikae"

        /** The run configuration type the console plugin brings, when that plugin is installed and on. */
        fun consoleType(): ConfigurationType? = ConfigurationType.CONFIGURATION_TYPE_EP.extensionList.firstOrNull {
            (it.javaClass.classLoader as? PluginAwareClassLoader)?.pluginId == PluginId.getId(CONSOLE_PLUGIN)
        }
    }
}
