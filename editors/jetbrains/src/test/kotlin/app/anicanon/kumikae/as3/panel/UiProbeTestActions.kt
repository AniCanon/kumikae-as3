package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.run.KumikaeRunType
import app.anicanon.kumikae.as3.testing.As3TestConsole
import app.anicanon.kumikae.as3.testing.As3TestLineMarker
import app.anicanon.kumikae.as3.testing.As3TestProducer
import app.anicanon.kumikae.as3.testing.As3TestsConfiguration
import app.anicanon.kumikae.as3.testing.RunGameTestsAction
import com.intellij.execution.DefaultExecutionTarget
import com.intellij.execution.ExecutionTargetManager
import com.intellij.execution.PsiLocation
import com.intellij.execution.actions.ConfigurationContext
import com.intellij.execution.actions.RunConfigurationProducer
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiManager
import com.intellij.psi.search.GlobalSearchScope
import java.nio.file.Files
import java.nio.file.Path

/** Real PSI, gutter contributor, native context producer and result source locator. */
internal object UiProbeTestActions {
    fun run(project: Project, destination: Path) {
        val root = Path.of(checkNotNull(project.basePath))
        val source = root.resolve("CaseActionProbe.as")
        val manifest = root.resolve("kas3-tests.json")
        val text = "package { public class CaseActionProbe { " +
            "[Test] public function gapJump():void { function nested():void {} } " +
            "[Test] public function magnet():void {} public function helper():void {} } }"
        Files.writeString(source, text)
        Files.writeString(
            manifest,
            """{"version":1,"tests":[{"name":"Models","source":"CaseActionProbe.as","cases":[
            {"source":"CaseActionProbe.as","suite":"Course","name":"gapJump","method":"gapJump"},
            {"source":"CaseActionProbe.as","suite":"Course","name":"magnet","method":"magnet"}]}]}""",
        )
        try {
            ApplicationManager.getApplication().invokeAndWait {
                val vfs = LocalFileSystem.getInstance()
                vfs.refreshAndFindFileByNioFile(manifest)
                val file = checkNotNull(vfs.refreshAndFindFileByNioFile(source))
                val psi = checkNotNull(PsiManager.getInstance(project).findFile(file))
                val classLeaf = checkNotNull(psi.findElementAt(text.indexOf("class")))
                val methodLeaf = checkNotNull(psi.findElementAt(text.indexOf("function gapJump")))
                val helperLeaf = checkNotNull(psi.findElementAt(text.indexOf("function helper")))
                val nestedLeaf = checkNotNull(psi.findElementAt(text.indexOf("function nested")))
                val gutter = As3TestLineMarker()
                check(gutter.getInfo(classLeaf) != null) { "File gutter missing" }
                check(gutter.getInfo(methodLeaf) != null) { "Case gutter missing" }
                check(gutter.getInfo(nestedLeaf) == null) { "Nested helper has a test gutter" }
                check(gutter.getInfo(helperLeaf) == null) { "Helper has a test gutter" }
                val producer = As3TestProducer()
                val android = KumikaeRunType.factory(KumikaeRunType.Kind.ANDROID).createTemplateConfiguration(project)
                val context = ConfigurationContext.createEmptyContextForLocation(PsiLocation(methodLeaf))

                @Suppress("UNCHECKED_CAST")
                val platformProducer = producer as RunConfigurationProducer<RunConfiguration>
                check(!platformProducer.isConfigurationFromContext(android, context))
                fun configured(element: PsiElement): As3TestsConfiguration {
                    val context = ConfigurationContext.createEmptyContextForLocation(PsiLocation(element))
                    val config = checkNotNull(
                        producer.createConfigurationFromContext(context),
                    ).configuration as As3TestsConfiguration
                    check(producer.isConfigurationFromContext(config, context))
                    return config
                }
                check(configured(classLeaf).options.cases == "Models::Course::gapJump\nModels::Course::magnet")
                check(configured(psi).options.cases == "Models::Course::gapJump\nModels::Course::magnet")
                val config = configured(methodLeaf)
                check(
                    ExecutionTargetManager.getInstance(project).getTargetsFor(config)
                        .any { it == DefaultExecutionTarget.INSTANCE },
                ) {
                    "Gutter tests need a local execution target."
                }
                val action = checkNotNull(gutter.getInfo(methodLeaf)).actions.single()
                check(action is RunGameTestsAction)
                check(config.options.cases == "Models::Course::gapJump")
                val properties = As3TestConsole(config, DefaultRunExecutor.getRunExecutorInstance())
                val location = properties.getTestLocator().getLocation(
                    "kumikae-as3",
                    source.toString() + "?method=gapJump",
                    project,
                    GlobalSearchScope.allScope(project),
                ).single()
                check(location.psiElement.textOffset == methodLeaf.textOffset) { "Result opens the wrong method" }
                val failure =
                    "Error: failed\n\tat Assert/assertTrue()[/framework/app/anicanon/kumikae/testing/Assert.as:5]\n" +
                        "\tat CaseActionProbe/gapJump()[" + source + ":1]"
                val descriptor = properties.getErrorNavigatable(location, failure) as? OpenFileDescriptor
                check(descriptor?.file == file && descriptor.line == 0) { "Failure does not open its source line" }
            }
            UiProbeAuthoring.run(project, root)
            Files.writeString(
                destination.resolve("test-actions.txt"),
                "passed: file and method gutters, context selection, helper exclusion and method navigation\n",
            )
        } finally {
            Files.deleteIfExists(source)
            Files.deleteIfExists(manifest)
        }
    }
}
