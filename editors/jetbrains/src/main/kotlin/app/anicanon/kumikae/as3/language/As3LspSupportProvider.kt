package app.anicanon.kumikae.as3.language

import app.anicanon.kumikae.as3.PluginFiles
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.ide.util.PropertiesComponent
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspIntegrationProvider
import com.intellij.platform.lsp.api.ProjectWideLspClientDescriptor
import java.nio.file.Path
import java.util.concurrent.ConcurrentHashMap

private const val EXTENSION = "as"

/**
 * Starts `kas3c lsp` for a project's `.as` files. Errors as you type, go to definition, find usages,
 * hover and the outline all come from the compiler itself, so the IDE and `kas3c check` never disagree.
 */
class As3LspSupportProvider : LspIntegrationProvider {
    private val warned = ConcurrentHashMap.newKeySet<String>()

    override fun fileOpened(
        project: Project,
        file: VirtualFile,
        clientStarter: LspIntegrationProvider.LspClientStarter,
    ) {
        if (file.extension != EXTENSION) return
        val base = project.basePath ?: return
        val compiler = CompilerLocator.find(
            Path.of(base),
            PropertiesComponent.getInstance().getValue(COMPILER_PATH).orEmpty(),
            System.getenv("PATH").orEmpty(),
            Path.of(System.getProperty("user.home")),
            PluginFiles.native(),
        )
        if (compiler == null) {
            if (warned.add(base)) warn(project)
        } else {
            clientStarter.ensureClientStarted(As3LspClientDescriptor(project, compiler))
            // The file Godot's declarations are kept in, so that the first go to declaration finds it.
            ApplicationManager.getApplication().executeOnPooledThread {
                repeat(DECLARATION_LOOKS) {
                    if (LocalFileSystem.getInstance().refreshAndFindFileByNioFile(DECLARATIONS) !=
                        null
                    ) {
                        return@executeOnPooledThread
                    }
                    Thread.sleep(DECLARATION_WAIT_MILLIS)
                }
            }
        }
    }

    private fun warn(project: Project) {
        NotificationGroupManager.getInstance().getNotificationGroup(NOTIFICATIONS).createNotification(
            "ActionScript 3",
            "The compiler, kas3c, was not found, so there are no errors, navigation or hover for .as files. " +
                "Build it (cmake --build compiler/build) or put it on the search path.",
            NotificationType.WARNING,
        ).notify(project)
    }

    private companion object {
        const val COMPILER_PATH = "kumikae.as3.compilerPath"
        const val NOTIFICATIONS = "Kumikae AS3"
        const val DECLARATION_LOOKS = 20
        const val DECLARATION_WAIT_MILLIS = 500L

        // Where `kas3c lsp` writes Godot's API as AS3, for an editor to open.
        val DECLARATIONS: Path = Path.of(System.getProperty("java.io.tmpdir"), "kas3c-declarations", "godot.as")
    }
}

private class As3LspClientDescriptor(project: Project, private val compiler: Path) :
    ProjectWideLspClientDescriptor(project, "ActionScript 3") {
    override fun isSupportedFile(file: VirtualFile) = file.extension == EXTENSION

    override fun createCommandLine() = GeneralCommandLine(compiler.toString(), "lsp")

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
