package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.language.As3LspSupportProvider
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.platform.lsp.api.LspClientManager
import com.intellij.platform.lsp.api.LspServerNotificationsHandler
import org.eclipse.lsp4j.PublishDiagnosticsParams
import java.lang.reflect.Proxy

/** Runs the installed descriptor's receive boundary on the background probe thread. */
internal object UiProbeRiderDiagnostics {
    fun checkReceiveBoundary(project: Project) {
        val application = ApplicationManager.getApplication()
        check(!application.isReadAccessAllowed) { "The receive test must start outside a read action" }
        val descriptor = LspClientManager.getInstance(project)
            .getClients(As3LspSupportProvider::class.java).single().descriptor
        val params = PublishDiagnosticsParams("file:///diagnostic-probe.as", emptyList(), 1)
        var received = false
        val handler = Proxy.newProxyInstance(
            LspServerNotificationsHandler::class.java.classLoader,
            arrayOf(LspServerNotificationsHandler::class.java),
        ) { _, method, arguments ->
            check(method.name == "publishDiagnostics") { "Unexpected notification: ${method.name}" }
            check(application.isReadAccessAllowed) {
                "Diagnostic version validation and cache insertion can race a document write"
            }
            check(arguments?.single() === params) { "The adapter changed the diagnostic publication" }
            received = true
            null
        } as LspServerNotificationsHandler
        descriptor.createLsp4jClient(handler).publishDiagnostics(params)
        check(received) { "The diagnostic publication was dropped" }
    }
}
