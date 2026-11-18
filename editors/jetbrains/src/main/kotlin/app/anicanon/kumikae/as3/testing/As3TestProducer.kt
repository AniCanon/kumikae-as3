package app.anicanon.kumikae.as3.testing

import app.anicanon.kumikae.as3.run.KumikaeRunType
import com.intellij.execution.actions.ConfigurationContext
import com.intellij.execution.actions.LazyRunConfigurationProducer
import com.intellij.execution.lineMarker.RunLineMarkerContributor
import com.intellij.icons.AllIcons
import com.intellij.openapi.util.Ref
import com.intellij.psi.PsiElement

class As3TestProducer :
    LazyRunConfigurationProducer<com.intellij.execution.configurations.RunConfiguration>(),
    com.intellij.openapi.project.DumbAware {
    override fun getConfigurationFactory() = KumikaeRunType.factory(KumikaeRunType.Kind.TESTS)
    override fun setupConfigurationFromContext(
        configuration: com.intellij.execution.configurations.RunConfiguration,
        context: ConfigurationContext,
        sourceElement: Ref<PsiElement>,
    ): Boolean {
        if (configuration !is As3TestsConfiguration) return false
        val element = context.psiLocation ?: return false
        sourceElement.set(element)
        return configureTestScope(configuration, element)
    }
    override fun isConfigurationFromContext(
        configuration: com.intellij.execution.configurations.RunConfiguration,
        context: ConfigurationContext,
    ): Boolean {
        if (configuration !is As3TestsConfiguration) return false
        val tests = TestDiscovery.forElement(context.psiLocation ?: return false)
        return tests.isNotEmpty() &&
            java.nio.file.Path.of(
                configuration.project.basePath.orEmpty(),
            ).resolve(configuration.options.manifest).normalize() ==
            tests.first().manifest.toNioPath() &&
            configuration.options.cases == tests.joinToString("\n") { it.name }
    }
}

internal fun configureTestScope(configuration: As3TestsConfiguration, element: PsiElement): Boolean {
    val file =
        (element as? com.intellij.psi.PsiDirectory)?.virtualFile ?: element.containingFile?.virtualFile
            ?: return false
    val tests = TestDiscovery.forElement(element)
    if (tests.isEmpty()) return false
    val manifest = tests.first().manifest
    val root = configuration.project.basePath?.let(java.nio.file.Path::of)
    configuration.options.manifest = root?.relativize(manifest.toNioPath())?.toString() ?: manifest.path
    configuration.options.cases = tests.joinToString("\n") { it.name }
    configuration.name = if (tests.size == 1) tests.single().name else "Tests in ${file.name}"
    return true
}

class As3TestLineMarker : RunLineMarkerContributor() {
    override fun getInfo(element: PsiElement): Info? {
        if (element.firstChild != null || element.text !in listOf("class", "function")) return null
        val file = element.containingFile?.virtualFile ?: return null
        if (file.extension != "as") return null
        val known = TestDiscovery.of(element.containingFile)
        val source = known.source
        val tests = known.at(element.textOffset)
        if (tests.isEmpty()) return null
        if (element.text == "class" && element.textOffset != source.classOffset) return null
        if (element.text == "function" &&
            (tests.none { it.method != null } || source.methodAt(element.textOffset)?.offset != element.textOffset)
        ) {
            return null
        }
        val run = RunGameTestsAction(
            element,
            if (element.text ==
                "class"
            ) {
                "Run tests in ${file.name}"
            } else {
                "Run ${source.methodAt(element.textOffset)?.name}"
            },
        )
        return Info(AllIcons.RunConfigurations.TestState.Run, arrayOf(run)) {
            if (element.text ==
                "class"
            ) {
                "Run all tests in ${file.name}"
            } else {
                "Run ${source.methodAt(element.textOffset)?.name}"
            }
        }
    }
}
