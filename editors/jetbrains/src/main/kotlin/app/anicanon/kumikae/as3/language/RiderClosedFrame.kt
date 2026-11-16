package app.anicanon.kumikae.as3.language

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.diagnostic.Logger
import com.intellij.openapi.project.Project
import com.intellij.openapi.wm.WindowManager
import java.beans.PropertyChangeListenerProxy

/** Captures one closing frame; never resolves a new project's frame during deferred cleanup. */
internal class RiderClosedFrame(project: Project) {
    private val frame = WindowManager.getInstance().getFrame(project)
    private val root = frame?.rootPane
    private val access = frame?.let { RiderFrameAccess(it, checkNotNull(root)) }

    fun afterClose(project: Project) {
        ApplicationManager.getApplication().invokeLater({
            repairRider("closed frame") { release(project) }
        }, ModalityState.any())
    }

    private fun release(project: Project) {
        if (frame == null || root == null || access == null) return
        val helperDisposed = access.helperDisposed()
        val sameRoot = frame.rootPane === root
        val sameOwner = access.ownsCurrentHelper()
        if (!canReleaseClosedFrame(project.isDisposed, helperDisposed, sameRoot, frame.isDisplayable) ||
            (sameRoot && !sameOwner)
        ) {
            Logger.getInstance("kumikae.RiderLifetime").warn(
                "Rider frame cleanup skipped: projectDisposed=${project.isDisposed}, " +
                    "helperDisposed=$helperDisposed, sameRoot=$sameRoot, displayable=${frame.isDisplayable}, " +
                    "sameOwner=$sameOwner",
            )
            return
        }
        root.contentPane.removeAll()
        root.layeredPane.removeAll()
        root.jMenuBar = null
        root.propertyChangeListeners.filterIsInstance<PropertyChangeListenerProxy>().forEach { proxy ->
            val name = proxy.listener.javaClass.name
            if (name.startsWith("com.intellij.openapi.wm.impl.ProjectFrameCustomHeaderHelper") ||
                name.startsWith("com.intellij.openapi.wm.impl.customFrameDecorations.header.MacToolbarFrameHeader")
            ) {
                root.removePropertyChangeListener(proxy.propertyName, proxy.listener)
            }
        }
        access.release()
    }
}

internal fun canReleaseClosedFrame(
    projectDisposed: Boolean,
    helperDisposed: Boolean,
    sameRoot: Boolean,
    displayable: Boolean,
): Boolean = projectDisposed && helperDisposed && (!sameRoot || !displayable)
