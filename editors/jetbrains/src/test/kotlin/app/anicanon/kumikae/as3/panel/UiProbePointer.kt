package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ModalityState
import com.intellij.util.ui.JBUI
import java.awt.event.InputEvent
import java.awt.event.MouseEvent

internal object UiProbePointer {
    /** Real Swing drag phases must change the picture before mouse release. */
    fun drag(screen: SimulatorScreen, painted: () -> Int): Int {
        fun onEdt(work: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(work, ModalityState.any())
        fun event(kind: Int, yRatio: Double) {
            onEdt {
                val frame = checkNotNull(screen.frame)
                val place = DeviceFrame(
                    frame.image.width,
                    frame.image.height,
                    screen.width,
                    screen.height,
                    JBUI.scale(18),
                    island = true,
                ).screen
                screen.dispatchEvent(
                    MouseEvent(
                        screen, kind, System.currentTimeMillis(),
                        if (kind == MouseEvent.MOUSE_RELEASED) 0 else InputEvent.BUTTON1_DOWN_MASK,
                        (place.x + place.width * .5).toInt(), (place.y + place.height * yRatio).toInt(),
                        1, false, if (kind == MouseEvent.MOUSE_DRAGGED) MouseEvent.NOBUTTON else MouseEvent.BUTTON1,
                    ),
                )
            }
        }
        val before = painted()
        event(MouseEvent.MOUSE_PRESSED, .8)
        try {
            repeat(40) { index ->
                Thread.sleep(17)
                event(MouseEvent.MOUSE_DRAGGED, .8 - .5 * (index + 1) / 40)
            }
            val changed = painted() - before
            check(changed > 5) { "Live drag did not change the picture before release" }
            return changed
        } finally {
            event(MouseEvent.MOUSE_RELEASED, .3)
        }
    }
}
