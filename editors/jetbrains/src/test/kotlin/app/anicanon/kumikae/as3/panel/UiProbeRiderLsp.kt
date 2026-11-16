package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.language.As3LspSupportProvider
import com.intellij.openapi.project.Project
import com.intellij.platform.lsp.api.LspClientManager

internal object UiProbeRiderLsp {
    fun ready(project: Project): Boolean = LspClientManager.getInstance(project)
        .getClients(As3LspSupportProvider::class.java).any { it.initializeResult != null }
}
