package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.util.Disposer
import com.intellij.ui.components.JBTextArea
import com.intellij.util.ui.JBUI
import java.awt.EventQueue
import java.awt.event.HierarchyEvent
import java.awt.event.HierarchyListener
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
    private var requested: SimulatorDevice? = null
    fun openDevice(device: SimulatorDevice) {
        requested = device
        state.listShown = false
        openRequested()
    }
    private fun openRequested() {
        val device = requested ?: return
        if (controller.endpointFile.isEmpty() || controller.working || !component.isShowing) return
        requested = null
        controller.open(device)
    }
    private val timer = Timer(POLL_MILLIS) {
        openRequested()
        controller.poll()
        controller.frameTick() // Startup, disconnect and retry use the status cadence.
    }
    private val frames = FrameDispatch({ EventQueue.invokeLater(it) }, controller::frameTick)

    init {
        controller.onFramesReady = frames::request
        controller.onFrame = { screen.frame = it }
        component = composePanel(controller) { SimulatorView(controller, state, prompts, screen, console) }
        val visibility = HierarchyListener { event ->
            if ((event.changeFlags and HierarchyEvent.SHOWING_CHANGED.toLong()) == 0L) return@HierarchyListener
            if (component.isShowing) {
                controller.shown()
                timer.start()
                controller.frameTick()
            } else {
                requested = null
                controller.hidden()
                timer.stop()
            }
        }
        Disposer.register(controller) {
            component.removeHierarchyListener(visibility)
            screen.frame = null
            controller.onFrame = {}
            timer.stop()
            controller.onFramesReady = {}
        }
        component.addHierarchyListener(visibility)
    }

    private companion object {
        // Status and logs have their own cadence; pictures use a newest-frame slot.
        const val POLL_MILLIS = 500
    }
}
