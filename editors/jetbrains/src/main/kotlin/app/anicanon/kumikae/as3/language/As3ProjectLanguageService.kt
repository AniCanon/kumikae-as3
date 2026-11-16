package app.anicanon.kumikae.as3.language

import app.anicanon.kumikae.as3.PluginFiles
import com.intellij.ide.util.PropertiesComponent
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.Disposable
import com.intellij.openapi.components.Service
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.util.Alarm
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicBoolean

/** Launch configuration and VFS work belong to the project, independently of its LSP adapter. */
@Service(Service.Level.PROJECT)
internal class As3ProjectLanguageService(private val project: Project) : Disposable {
    // Shared by the project's LSP4IJ launcher, which sees what is sent, and its client, which is answered.
    val sentVersions = SentVersions()
    private val warned = AtomicBoolean()
    private val warnedClient = AtomicBoolean()
    private val alarm = Alarm(Alarm.ThreadToUse.POOLED_THREAD, this)
    private val declarations = DeclarationRefresh(
        schedule = { delay, work -> alarm.addRequest(Runnable(work), delay) },
        cancel = alarm::cancelAllRequests,
        refresh = {
            !project.isDisposed && LocalFileSystem.getInstance().refreshAndFindFileByNioFile(
                Path.of(System.getProperty("java.io.tmpdir"), "kas3c-declarations", "godot.as"),
            ) != null
        },
    )

    fun compiler(): Path? {
        val base = project.basePath ?: return null
        return CompilerLocator.find(
            Path.of(base),
            PropertiesComponent.getInstance().getValue("kumikae.as3.compilerPath").orEmpty(),
            System.getenv("PATH").orEmpty(),
            Path.of(System.getProperty("user.home")),
            PluginFiles.native(),
        )
    }

    fun refreshDeclarations() = declarations.start()

    fun warnMissingCompiler() = warnOnce(
        warned,
        "The compiler, kas3c, was not found, so there are no errors, navigation or hover for .as files. " +
            "Build it (cmake --build compiler/build) or put it on the search path.",
    )

    fun warnMissingClient() = warnOnce(
        warnedClient,
        "Install and enable LSP4IJ in Settings → Plugins for ActionScript 3 completion, errors and navigation. " +
            "Local syntax highlighting and paired typing remain available.",
    )

    private fun warnOnce(flag: AtomicBoolean, message: String) {
        if (project.isDisposed || !flag.compareAndSet(false, true)) return
        NotificationGroupManager.getInstance().getNotificationGroup("Kumikae AS3").createNotification(
            "ActionScript 3",
            message,
            NotificationType.WARNING,
        ).notify(project)
    }

    override fun dispose() = declarations.close()
}
