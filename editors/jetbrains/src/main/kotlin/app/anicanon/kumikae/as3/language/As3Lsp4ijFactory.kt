package app.anicanon.kumikae.as3.language

import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.redhat.devtools.lsp4ij.LanguageServerEnablementSupport
import com.redhat.devtools.lsp4ij.LanguageServerFactory
import com.redhat.devtools.lsp4ij.client.LanguageClientImpl
import com.redhat.devtools.lsp4ij.client.features.LSPClientFeatures
import com.redhat.devtools.lsp4ij.features.diagnostics.LSPDiagnosticsApplier
import com.redhat.devtools.lsp4ij.server.CannotStartProcessException
import com.redhat.devtools.lsp4ij.server.OSProcessStreamConnectionProvider
import com.redhat.devtools.lsp4ij.server.StreamConnectionProvider
import org.eclipse.lsp4j.InitializeParams

/** Loaded only with the optional LSP4IJ dependency. Rider retains its native client. */
class As3Lsp4ijFactory :
    LanguageServerFactory,
    LanguageServerEnablementSupport {
    override fun createLanguageClient(project: Project): LanguageClientImpl =
        As3Lsp4ijClient(project) { file, document ->
            LSPDiagnosticsApplier.getInstance(project).scheduleRefresh(file, document)
        }

    override fun createClientFeatures(): LSPClientFeatures = object : LSPClientFeatures() {
        override fun initializeParams(params: InitializeParams) {
            super.initializeParams(params)
            // LSP4IJ filters mismatched versions, but does not advertise that support by default.
            params.capabilities.textDocument?.publishDiagnostics?.versionSupport = true
        }
    }.setCompletionFeature(As3Lsp4ijCompletion()).setDiagnosticFeature(As3Lsp4ijDiagnostics())

    override fun createConnectionProvider(project: Project): StreamConnectionProvider = As3Lsp4ijConnection(project)

    override fun isEnabled(project: Project): Boolean =
        PropertiesComponent.getInstance(project).getBoolean(ENABLED, true)

    override fun setEnabled(enabled: Boolean, project: Project) {
        PropertiesComponent.getInstance(project).setValue(ENABLED, enabled, true)
    }

    private companion object {
        const val ENABLED = "kumikae.as3.lsp4ij.enabled"
    }
}

private class As3Lsp4ijConnection(private val project: Project) : OSProcessStreamConnectionProvider() {
    override fun start() {
        val service = project.getService(As3ProjectLanguageService::class.java)
        val compiler = service.compiler()
        if (compiler == null) {
            service.warnMissingCompiler()
            throw CannotStartProcessException("The ActionScript 3 compiler, kas3c, was not found")
        }
        commandLine = serverCommandLine(compiler)
        super.start()
        service.refreshDeclarations()
    }

    override fun getInitializationOptions(rootUri: VirtualFile?): Any = serverOptions()
}
