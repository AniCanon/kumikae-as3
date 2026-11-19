package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.run.IosDevice
import app.anicanon.kumikae.as3.run.IosExecutionTarget
import app.anicanon.kumikae.as3.run.IosUnavailableTarget
import app.anicanon.kumikae.as3.run.KumikaeRunType
import app.anicanon.kumikae.as3.run.SimulatorDestination
import app.anicanon.kumikae.as3.run.SimulatorRunConfiguration
import app.anicanon.kumikae.as3.run.SimulatorRunEditor
import com.intellij.execution.ExecutionTarget
import com.intellij.execution.ExecutionTargetManager
import com.intellij.execution.ExecutionTargetProvider
import com.intellij.execution.RunManager
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.ide.impl.OpenProjectTask
import com.intellij.ide.impl.ProjectUtil
import com.intellij.ide.impl.TrustedPathsSettings
import com.intellij.ide.ui.LafManager
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.ex.ProjectManagerEx
import com.intellij.openapi.startup.StartupManager
import com.intellij.openapi.util.Disposer
import java.awt.Container
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import javax.imageio.ImageIO
import javax.swing.JFrame

/** Exercises native target selection and the production configuration editor without deploying an app. */
internal object UiProbeIosTargets {
    fun run(destination: Path) {
        val folder = Files.createTempDirectory("kas3-ios-target-probe")
        val trust = TrustedPathsSettings.getInstance()
        val trustedPaths = trust.getTrustedPaths().toList()
        trust.addTrustedPath(folder.toString())
        val project = checkNotNull(
            ProjectUtil.openOrImport(
                folder,
                OpenProjectTask { forceOpenInNewFrame = true },
            ),
        )
        val deadline = System.nanoTime() + 30_000_000_000L
        while (!StartupManager.getInstance(project).allActivitiesPassedFuture.isCompleted) {
            check(System.nanoTime() < deadline) { "Probe project startup did not finish" }
            Thread.sleep(50)
        }
        val manager = LafManager.getInstance()
        val original = manager.currentUIThemeLookAndFeel
        try {
            for (name in listOf("Light", "Dark")) {
                val theme = manager.installedThemes.toList().first { it.name.equals(name, ignoreCase = true) }
                ApplicationManager.getApplication().invokeAndWait {
                    manager.setCurrentUIThemeLookAndFeel(theme)
                    manager.updateUI()
                }
                check(project, destination)
                Files.move(
                    destination.resolve("ios-run-editor.png"),
                    destination.resolve("ios-run-editor-${name.lowercase()}.png"),
                    java.nio.file.StandardCopyOption.REPLACE_EXISTING,
                )
            }
        } finally {
            ApplicationManager.getApplication().invokeAndWait {
                if (original != null) manager.setCurrentUIThemeLookAndFeel(original)
                manager.updateUI()
                ProjectManagerEx.getInstanceEx().forceCloseProject(project, false)
            }
            trust.setTrustedPaths(trustedPaths)
            check(folder.toFile().deleteRecursively())
        }
    }

    fun check(project: Project, destination: Path) {
        ApplicationManager.getApplication().invokeAndWait {
            val lifetime = Disposer.newDisposable()
            val phone =
                IosExecutionTarget(IosDevice("probe-phone", "Probe iPhone", true, "connected"))
            val simulator =
                SimulatorDestination(SimulatorDevice("probe-sim", "Probe Simulator", "iOS", "Booted"))
            val unavailable = IosUnavailableTarget("Probe discovery failed")
            val provider = object : ExecutionTargetProvider() {
                override fun getTargets(project: Project, configuration: RunConfiguration): List<ExecutionTarget> =
                    if (configuration is SimulatorRunConfiguration) {
                        listOf(
                            phone,
                            simulator,
                            unavailable,
                        )
                    } else {
                        emptyList()
                    }
            }
            ExecutionTargetProvider.EXTENSION_NAME.point.registerExtension(provider, lifetime)
            val manager = RunManager.getInstance(project)
            val previous = manager.selectedConfiguration
            val settings = manager.createConfiguration(
                "iOS target probe",
                KumikaeRunType.factory(KumikaeRunType.Kind.SIMULATOR),
            )
            val configuration = settings.configuration as SimulatorRunConfiguration
            manager.addConfiguration(settings)
            manager.selectedConfiguration = settings
            val editor = SimulatorRunEditor()
            val host = JFrame()
            try {
                val targets = ExecutionTargetManager.getTargetsToChooseFor(project, configuration)
                check(targets.any { it.id == phone.id } && targets.any { it.id == simulator.id })
                check(
                    targets.any {
                        it.id == unavailable.id
                    },
                ) { "Discovery error is hidden by native target filtering" }
                ExecutionTargetManager.setActiveTarget(project, phone)
                check(ExecutionTargetManager.getActiveTarget(project).id == phone.id)
                check(ExecutionTargetManager.canRun(configuration, phone))
                ExecutionTargetManager.setActiveTarget(project, simulator)
                check(ExecutionTargetManager.getActiveTarget(project).id == simulator.id)
                configuration.options.deviceAppPath = "/probe/Signed.app"
                editor.resetFrom(configuration)
                configuration.options.deviceAppPath = ""
                editor.applyTo(configuration)
                check(configuration.options.deviceAppPath == "/probe/Signed.app")
                host.contentPane.add(editor.component)
                host.pack()
                host.setSize(780, 330)
                layout(host)
                val image = BufferedImage(780, 330, BufferedImage.TYPE_INT_RGB)
                image.createGraphics().useGraphics { host.contentPane.printAll(it) }
                ImageIO.write(image, "png", destination.resolve("ios-run-editor.png").toFile())
                Files.writeString(
                    destination.resolve("ios-targets.txt"),
                    "passed: native phone/simulator target selection and configuration editor\n",
                )
            } finally {
                host.dispose()
                Disposer.dispose(editor)
                manager.selectedConfiguration = previous
                manager.removeConfiguration(settings)
                Disposer.dispose(lifetime)
            }
        }
    }

    private fun layout(container: Container) {
        container.doLayout()
        container.components.filterIsInstance<Container>().forEach(::layout)
    }

    private fun java.awt.Graphics2D.useGraphics(block: (java.awt.Graphics2D) -> Unit) {
        try {
            block(this)
        } finally {
            dispose()
        }
    }
}
