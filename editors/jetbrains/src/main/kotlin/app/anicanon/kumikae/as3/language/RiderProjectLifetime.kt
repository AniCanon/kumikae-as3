package app.anicanon.kumikae.as3.language

import com.intellij.analysis.problemsView.toolWindow.ProblemsViewPanelProvider
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationInfo
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.service
import com.intellij.openapi.extensions.LoadingOrder
import com.intellij.openapi.extensions.ProjectExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.ProjectManagerListener
import com.intellij.openapi.util.Key
import com.intellij.openapi.wm.ToolWindowManager
import kotlinx.coroutines.CoroutineScope

internal fun needsRiderLifetimeRepair(): Boolean = ApplicationInfo.getInstance().build.asString() == "RD-262.10968.170"

private val CLOSING_PROJECT = Key.create<Boolean>("kumikae.rider.project.closing")

internal fun isRiderProjectClosing(project: Project): Boolean =
    project.isDisposed || project.getUserData(CLOSING_PROJECT) == true

internal class RiderLifetimeListener : ProjectManagerListener {
    override fun projectClosing(project: Project) {
        if (!needsRiderLifetimeRepair()) return
        project.putUserData(CLOSING_PROJECT, true)
        releaseRiderProjectFocus(project)
        repairRider("frame capture") { project.putUserData(CLOSING_FRAME, RiderClosedFrame(project)) }
    }

    override fun projectClosed(project: Project) {
        releaseRiderProjectFocus(project)
        val frame = project.getUserData(CLOSING_FRAME)
        project.putUserData(CLOSING_FRAME, null)
        frame?.afterClose(project)
    }

    // Deprecated in favour of a startup activity, which runs too late: the native startup activities this
    // replaces have run by then. No undeprecated hook runs before them in the builds this plugin supports.
    @Suppress("OVERRIDE_DEPRECATION")
    override fun projectOpened(project: Project) {
        if (needsRiderLifetimeRepair()) {
            project.putUserData(CLOSING_PROJECT, null)
            repairRider("feedback ownership") {
                ApplicationManager.getApplication().service<RiderFeedbackLifetime>().install()
            }
            repairRider("database startup ownership") {
                ApplicationManager.getApplication().service<RiderDatabaseLifetime>().install()
            }
            repairRider("Problems panel ownership") { project.service<RiderProjectLifetime>().start() }
        }
    }

    companion object {
        private val CLOSING_FRAME = Key.create<RiderClosedFrame>("kumikae.rider.closing.frame")
    }
}

@Service(Service.Level.PROJECT)
internal class RiderProjectLifetime(private val project: Project, val scope: CoroutineScope) : Disposable {
    private var started = false
    fun start() {
        if (started) return
        val point = ProjectExtensionPointName<ProblemsViewPanelProvider>(
            "com.intellij.problemsViewPanelProvider",
        ).getPoint(project)
        val original = point.extensionList.singleOrNull {
            it.javaClass.name == "com.jetbrains.rider.problemsView.views.panels.CurrentFileProblemsPanelProvider"
        } ?: error("Rider Current File Problems provider is missing")
        val replacement = RiderOwnedPanelProvider(
            { checkNotNull(ToolWindowManager.getInstance(project).getToolWindow("Problems View")).disposable },
            original,
        )
        replaceRiderExtension(point, original, replacement, this, LoadingOrder.FIRST)
        started = true
    }

    override fun dispose() = Unit
}
