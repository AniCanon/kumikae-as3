package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.util.Disposer
import com.intellij.ui.components.JBTextArea
import com.intellij.util.ui.JBUI
import javax.swing.JPanel
import javax.swing.Timer

/** Mounts the composed view, keeps the two native controls and schedules polling while the window shows. */
internal class SimulatorToolWindow(val controller: SimulatorWindowController) {
    val component: JPanel
    private val screen = SimulatorScreen(controller::gesture)
    private val console = JBTextArea().apply {
        isEditable = false
        lineWrap = true
        margin = JBUI.insets(4)
    }
    private val timer = Timer(POLL_MILLIS) { controller.poll() }

    init {
        component = composePanel(controller) { SimulatorView(controller, screen, console) }
        Disposer.register(controller) { timer.stop() }
        component.addHierarchyListener {
            if (component.isShowing) {
                controller.shown()
                timer.start()
            } else {
                controller.hidden()
                timer.stop()
            }
        }
    }

    private companion object {
        // A capture takes about half a second, so a faster timer would only queue refused polls.
        const val POLL_MILLIS = 500
    }
}
