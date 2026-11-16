package app.anicanon.kumikae.as3.testing

import com.intellij.execution.actions.ConfigurationContext
import com.intellij.execution.actions.LazyRunConfigurationProducer
import com.intellij.execution.configurations.ConfigurationTypeUtil
import com.intellij.execution.lineMarker.ExecutorAction
import com.intellij.execution.lineMarker.RunLineMarkerContributor
import com.intellij.icons.AllIcons
import com.intellij.openapi.util.Ref
import com.intellij.psi.PsiElement

class As3TestProducer : LazyRunConfigurationProducer<As3TestsConfiguration>() {
    override fun getConfigurationFactory() =
        ConfigurationTypeUtil.findConfigurationType(As3TestsType::class.java).configurationFactories.single()
    override fun setupConfigurationFromContext(
        configuration: As3TestsConfiguration,
        context: ConfigurationContext,
        sourceElement: Ref<PsiElement>,
    ): Boolean {
        val file = context.psiLocation?.containingFile?.virtualFile ?: return false
        val tests = TestDiscovery.forElement(context.psiLocation ?: return false)
        if (tests.isEmpty()) return false
        configuration.options.manifest = tests.first().manifest.path
        configuration.options.cases = tests.joinToString("\n") { it.name }
        configuration.name = if (tests.size == 1) tests.single().name else "Tests in ${file.name}"
        return true
    }
    override fun isConfigurationFromContext(
        configuration: As3TestsConfiguration,
        context: ConfigurationContext,
    ): Boolean {
        val tests = TestDiscovery.forElement(context.psiLocation ?: return false)
        return tests.isNotEmpty() && configuration.options.manifest == tests.first().manifest.path &&
            configuration.options.cases == tests.joinToString("\n") { it.name }
    }
}

class As3TestLineMarker : RunLineMarkerContributor() {
    override fun getInfo(element: PsiElement): Info? {
        if (element.firstChild != null || element.text !in listOf("class", "function")) return null
        val file = element.containingFile?.virtualFile ?: return null
        if (file.extension != "as") return null
        val source = TestSource.read(element.containingFile.text)
        val tests = TestDiscovery.forElement(element)
        if (tests.isEmpty()) return null
        if (element.text == "class" && element.textOffset != source.classOffset) return null
        if (element.text == "function" &&
            (tests.none { it.method != null } || source.methodAt(element.textOffset)?.offset != element.textOffset)
        ) {
            return null
        }
        return Info(AllIcons.RunConfigurations.TestState.Run, ExecutorAction.getActions(0)) {
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
