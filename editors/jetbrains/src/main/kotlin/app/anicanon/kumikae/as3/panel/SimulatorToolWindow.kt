package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.util.Disposer
import com.intellij.ui.components.JBTextArea
import com.intellij.util.ui.JBUI
import javax.swing.JPanel
import javax.swing.Timer

/** Mounts the composed view, keeps the two native controls and schedules polling while the window shows. */
internal class SimulatorToolWindow(val controller: SimulatorWindowController, val prompts: SimulatorPrompts) {
    val component: JPanel
    val state = SimulatorPanelState()
    val screen = SimulatorScreen(controller::gesture)
    val console = JBTextArea().apply {
        isEditable = false
        lineWrap = true

        // Between words; a word wider than the console alone is still broken where it must be.
        wrapStyleWord = true
        margin = JBUI.insets(4)
    }
    private val timer = Timer(POLL_MILLIS) { controller.poll() }
    private val frames = Timer(8) { controller.frameTick() }

    init {
        controller.onFrame = { screen.frame = it }
        component = composePanel(controller) { SimulatorView(controller, state, prompts, screen, console) }
        Disposer.register(controller) {
            controller.onFrame = {}
            timer.stop()
            frames.stop()
        }
        component.addHierarchyListener {
            if (component.isShowing) {
                controller.shown()
                timer.start()
                frames.start()
            } else {
                controller.hidden()
                timer.stop()
                frames.stop()
            }
        }
    }

    private companion object {
        // Status and logs have their own cadence; pictures use a newest-frame slot.
        const val POLL_MILLIS = 500
    }
}
