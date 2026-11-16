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
        val tests = TestDiscovery.forFile(file)
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
        val file = context.psiLocation?.containingFile?.virtualFile ?: return false
        val tests = TestDiscovery.forFile(file)
        return tests.isNotEmpty() && configuration.options.manifest == tests.first().manifest.path &&
            configuration.options.cases == tests.joinToString("\n") { it.name }
    }
}

class As3TestLineMarker : RunLineMarkerContributor() {
    override fun getInfo(element: PsiElement): Info? {
        if (element.firstChild != null || element.text != "class") return null
        val file = element.containingFile?.virtualFile ?: return null
        if (file.extension != "as" || TestDiscovery.forFile(file).isEmpty()) return null
        return Info(AllIcons.RunConfigurations.TestState.Run, ExecutorAction.getActions(0)) {
            "Run registered AS3 tests"
        }
    }
}
