package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.lookup.LookupManager
import com.intellij.ide.IdeEventQueue
import com.intellij.ide.skipWindowDeactivationEvents
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.Project
import com.intellij.openapi.wm.WindowManager
import java.awt.event.WindowEvent

/** Exercises background-window cancellation through the real IDE popup manager. */
internal object UiProbeFocus {
    fun run(work: () -> Unit) {
        var previous = false
        UiProbeCompletion.edt {
            previous = skipWindowDeactivationEvents
            skipWindowDeactivationEvents = true
        }
        try {
            work()
        } finally {
            UiProbeCompletion.edt { skipWindowDeactivationEvents = previous }
        }
    }

    fun checkLookup(project: Project, editor: Editor) = UiProbeCompletion.edt {
        val lookup = checkNotNull(LookupManager.getActiveLookup(editor))
        val frame = checkNotNull(WindowManager.getInstance().getFrame(project))
        val popups = IdeEventQueue.getInstance().popupManager
        for (id in listOf(WindowEvent.WINDOW_DEACTIVATED, WindowEvent.WINDOW_LOST_FOCUS)) {
            popups.dispatch(WindowEvent(frame, id))
            check(LookupManager.getActiveLookup(editor) === lookup) {
                "Background sandbox deactivation dismissed completion"
            }
        }
    }
}
