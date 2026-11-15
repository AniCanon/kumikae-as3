package app.anicanon.kumikae.as3.language

import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspIntegrationProvider
import com.intellij.platform.lsp.api.ProjectWideLspClientDescriptor
import java.nio.file.Path

private const val EXTENSION = "as"

/**
 * Starts `kas3c lsp` for a project's `.as` files. Errors as you type, go to definition, find usages,
 * hover and the outline all come from the compiler itself, so the IDE and `kas3c check` never disagree.
 */
class As3LspSupportProvider : LspIntegrationProvider {

    override fun fileOpened(
        project: Project,
        file: VirtualFile,
        clientStarter: LspIntegrationProvider.LspClientStarter,
    ) {
        if (file.extension != EXTENSION) return
        val service = project.getService(As3ProjectLanguageService::class.java)
        val compiler = service.compiler()
        if (compiler == null) {
            service.warnMissingCompiler()
        } else {
            clientStarter.ensureClientStarted(As3LspClientDescriptor(project, compiler))
            service.refreshDeclarations()
        }
    }
}

/**
 * What the server is told at the start. Code in a hover is marked as this plugin's own language: marked
 * `actionscript`, Rider hands it to a JavaScript dialect of its own, which fails on it.
 */
internal fun serverOptions(): Map<String, String> = mapOf("codeLanguage" to As3Language.id)

private class As3LspClientDescriptor(project: Project, private val compiler: Path) :
    ProjectWideLspClientDescriptor(project, "ActionScript 3") {
    override fun isSupportedFile(file: VirtualFile) = file.extension == EXTENSION

    override fun createInitializationOptions(): Any = serverOptions()

    override fun createCommandLine(): GeneralCommandLine {
        // For looking into a feature that does not work in the editor: with this set to a folder, every
        // message to and from the server is also written there, as it went over the wire.
        val trace =
            System.getenv(TRACE)?.takeIf { it.isNotBlank() } ?: return GeneralCommandLine(compiler.toString(), "lsp")
        val script = "tee \"\$1/to-server.log\" | \"\$2\" lsp | tee \"\$1/from-server.log\""
        return GeneralCommandLine("/bin/sh", "-c", script, "sh", trace, compiler.toString())
    }

    /**
     * A definition in Godot's API is in a file the server keeps outside the project, which the IDE has
     * never seen: looked up as the IDE does by default, without a refresh, it is not found, and go to
     * declaration says there is none. A refresh is not allowed while the caller holds the read lock, so
     * there it is started and the lookup answers from what is known; the next one finds the file.
     */
    override fun findLocalFileByPath(path: String): VirtualFile? {
        val files = LocalFileSystem.getInstance()
        files.findFileByPath(path)?.let { return it }
        val application = ApplicationManager.getApplication()
        if (application.isReadAccessAllowed && !application.isDispatchThread) {
            application.executeOnPooledThread { files.refreshAndFindFileByPath(path) }
            return null
        }
        return files.refreshAndFindFileByPath(path)
    }
}

private const val TRACE = "KUMIKAE_AS3_LSP_TRACE"
