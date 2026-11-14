package app.anicanon.kumikae.as3.language

import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.ide.util.PropertiesComponent
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.project.Project
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
        )
        if (compiler == null) {
            if (warned.add(base)) warn(project)
        } else {
            clientStarter.ensureClientStarted(As3LspClientDescriptor(project, compiler))
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
    }
}

private class As3LspClientDescriptor(project: Project, private val compiler: Path) :
    ProjectWideLspClientDescriptor(project, "ActionScript 3") {
    override fun isSupportedFile(file: VirtualFile) = file.extension == EXTENSION

    override fun createCommandLine() = GeneralCommandLine(compiler.toString(), "lsp")
}
