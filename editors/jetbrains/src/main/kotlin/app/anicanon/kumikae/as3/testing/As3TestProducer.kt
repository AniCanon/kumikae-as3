package app.anicanon.kumikae.as3.testing

import app.anicanon.kumikae.as3.run.KumikaeRunType
import com.intellij.execution.actions.ConfigurationContext
import com.intellij.execution.actions.LazyRunConfigurationProducer
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.execution.lineMarker.RunLineMarkerContributor
import com.intellij.icons.AllIcons
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.util.Ref
import com.intellij.psi.PsiDirectory
import com.intellij.psi.PsiElement
import java.nio.file.Path

class As3TestProducer :
    LazyRunConfigurationProducer<RunConfiguration>(),
    DumbAware {
    override fun getConfigurationFactory() = KumikaeRunType.factory(KumikaeRunType.Kind.TESTS)
    override fun setupConfigurationFromContext(
        configuration: RunConfiguration,
        context: ConfigurationContext,
        sourceElement: Ref<PsiElement>,
    ): Boolean {
        if (configuration !is As3TestsConfiguration) return false
        val element = context.psiLocation ?: return false
        sourceElement.set(element)
        return configureTestScope(configuration, element)
    }
    override fun isConfigurationFromContext(configuration: RunConfiguration, context: ConfigurationContext): Boolean {
        if (configuration !is As3TestsConfiguration) return false
        val tests = TestDiscovery.forElement(context.psiLocation ?: return false)
        if (tests.isEmpty()) return false
        val configuredManifest = Path.of(configuration.project.basePath.orEmpty())
            .resolve(configuration.options.manifest)
            .toAbsolutePath()
            .normalize()
        val manifest = tests.first().manifest
        if (!manifest.isInLocalFileSystem) return false
        val discoveredManifest = manifest.toNioPath().toAbsolutePath().normalize()
        return configuredManifest == discoveredManifest &&
            configuration.options.cases == tests.joinToString("\n") { it.name }
    }
}

internal fun configureTestScope(configuration: As3TestsConfiguration, element: PsiElement): Boolean {
    val file = (element as? PsiDirectory)?.virtualFile ?: element.containingFile?.virtualFile ?: return false
    val tests = TestDiscovery.forElement(element)
    if (tests.isEmpty()) return false
    val manifest = tests.first().manifest
    val root = configuration.project.basePath?.let(Path::of)?.toAbsolutePath()?.normalize()
    val manifestPath = if (manifest.isInLocalFileSystem) manifest.toNioPath().toAbsolutePath().normalize() else null
    configuration.options.manifest = if (root != null && manifestPath != null && manifestPath.startsWith(root)) {
        root.relativize(manifestPath).toString()
    } else {
        manifest.path
    }
    configuration.options.cases = tests.joinToString("\n") { it.name }
    configuration.name = if (tests.size == 1) tests.single().name else "Tests in ${file.name}"
    return true
}

class As3TestLineMarker : RunLineMarkerContributor() {
    override fun getInfo(element: PsiElement): Info? {
        if (element.firstChild != null) return null
        val keyword = element.text
        if (keyword != "class" && keyword != "function") return null
        val file = element.containingFile?.virtualFile ?: return null
        if (file.extension != "as") return null
        val known = TestDiscovery.of(element.containingFile)
        val source = known.source
        val offset = element.textOffset
        val tests = known.at(offset)
        if (tests.isEmpty()) return null
        val isClass = keyword == "class"
        if (isClass && offset != source.classOffset) return null
        val method = if (isClass) null else source.methodAt(offset)
        if (!isClass && (tests.none { it.method != null } || method?.offset != offset)) return null
        val fileName = file.name
        val methodName = method?.name.orEmpty()
        val actionLabel = if (isClass) "Run tests in $fileName" else "Run $methodName"
        val tooltip = if (isClass) "Run all tests in $fileName" else "Run $methodName"
        val run = RunGameTestsAction(element, actionLabel)
        return Info(AllIcons.RunConfigurations.TestState.Run, arrayOf(run)) { tooltip }
    }
}
