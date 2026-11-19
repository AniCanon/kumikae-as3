package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.Native
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.TitleButton
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.Disposable
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.EventQueue
import java.awt.event.HierarchyListener
import javax.swing.Timer

internal class AndroidDeviceWindow(private val repository: AndroidEmulators) : Disposable {
    private val actions = BackgroundWorker("Android emulator actions")
    private val polling = BackgroundWorker("Android emulator status")
    private var devices by mutableStateOf(emptyList<AndroidAvd>())
    private var status by mutableStateOf("")
    private var selected by mutableStateOf<AndroidAvd?>(null)
    private var requestedSerial: String? = null
    private var disposed = false

    fun openAvd(device: AndroidAvd) {
        if (device.serial != null) {
            openSerial(device.serial)
        } else {
            requestedSerial = null
            choose(device)
        }
    }

    fun openSerial(serial: String) {
        if (selected?.serial == serial) return
        closeView()
        requestedSerial = serial
        refresh()
    }

    private var booting: String? = null
    private var bootDeadline = 0L
    private var generation = 0
    private var feed: AndroidDisplayFeed? = null
    internal val screen = AndroidScreen { phase, point ->
        feed?.touch(phase, point.x, point.y)
    }
    val component = composePanel(this) {
        Panel(PanelLayout.Border(), modifier = SwingModifier.emptyBorder(JBUI.insets(8))) {
            val current = selected
            if (current == null) {
                Column(SwingModifier.north()) {
                    for (device in devices) {
                        TitleButton(
                            device.name.replace('_', ' '),
                            if (device.serial == null) "Start emulator" else "Open running emulator",
                            { choose(device) },
                        )
                    }
                }
            } else {
                Panel(PanelLayout.Flow(), modifier = SwingModifier.north()) {
                    Button("Emulators", { closeView() })
                    Button("Shut Down", { shutdown(current) })
                }
                Native(screen, SwingModifier.center())
            }
            StatusLine(status, SwingModifier.south())
        }
    }
    private val refresh = Timer(2000) { refresh() }
    private val frames = FrameDispatch({ EventQueue.invokeLater(it) }) {
        val live = feed
        live?.poll()?.let { next ->
            val previous = screen.picture
            screen.picture = next
            if (previous != null) live.retire(previous)
        }
        live?.failure?.let {
            closeView()
            status = it
        }
    }
    private val visibility = HierarchyListener {
        if (disposed) return@HierarchyListener
        if (component.isShowing) {
            if (!refresh.isRunning) {
                refresh()
                refresh.start()
            }
        } else {
            refresh.stop()
            val resume = selected?.serial ?: requestedSerial
            closeView()
            requestedSerial = resume
        }
    }

    init {
        component.addHierarchyListener(visibility)
    }

    private fun refresh() {
        if (disposed || !component.isShowing) return
        val serial = selected?.serial
        polling.submit({
            val list = repository.list()
            list to serial?.takeIf { id -> list.any { it.serial == id } }?.let(repository::display)
        }) { result ->
            result.onSuccess { (found, rotation) ->
                if (disposed || !component.isShowing) return@onSuccess
                devices = found
                found.firstOrNull { it.serial != null && it.serial == requestedSerial }?.let {
                    if (!actions.isBusy) {
                        requestedSerial = null
                        choose(it)
                    }
                }
                if (serial != null && serial == selected?.serial && found.none { it.serial == serial }) {
                    closeView()
                    status = "Emulator disconnected."
                }
                if (booting != null && System.nanoTime() - bootDeadline >= 0) {
                    status = "The emulator did not start. Check its Android Virtual Device configuration."
                    booting = null
                }
                found.firstOrNull { it.name == booting && it.serial != null }?.let {
                    booting = null
                    choose(it)
                }
                if (rotation != null && serial == selected?.serial) screen.guest = rotation
                if (found.isEmpty()) status = "Create an Android Virtual Device in Android Device Manager."
            }.onFailure { status = it.message.orEmpty() }
        }
    }

    private fun choose(device: AndroidAvd) {
        val serial = device.serial
        if (serial == null) {
            if (booting != null) {
                status = "Another emulator is already starting."
                return
            }
            if (actions.submit({ repository.start(device.name) }) { result ->
                    if (result.isFailure) booting = null
                    status = result.exceptionOrNull()?.message ?: "Starting ${device.name}…"
                    refresh()
                }
            ) {
                booting = device.name
                bootDeadline = System.nanoTime() + 180_000_000_000L
                status = "Starting ${device.name}…"
            } else {
                status = "Another emulator action is already running."
            }
            return
        }
        if (actions.isBusy) {
            status = "Another emulator action is already running."
            return
        }
        val epoch = ++generation
        val edge = (component.width * 2).coerceIn(720, 1440)
        actions.submitOwned({ repository.open(serial, edge) }, { result ->
            result.onSuccess {
                if (disposed || generation != epoch || !component.isShowing) {
                    it.close()
                } else {
                    feed?.close()
                    screen.picture = null
                    screen.guest = null
                    feed = it
                    it.onUpdate = frames::request
                    frames.request()
                    requestedSerial = null
                    selected = device
                    status = device.name
                    refresh()
                }
            }.onFailure {
                requestedSerial = null
                status = it.message.orEmpty()
            }
        }, AndroidDisplayFeed::close)
    }

    private fun shutdown(device: AndroidAvd) {
        val serial = device.serial ?: return
        if (!actions.submit({ repository.shutdown(serial) }) { result ->
                status = result.exceptionOrNull()?.message ?: "Shut down ${device.name}."
                refresh()
            }
        ) {
            status = "Another emulator action is already running."
            return
        }
        closeView()
    }

    private fun closeView() {
        generation++
        requestedSerial = null
        screen.cancelTouch()
        screen.guest = null
        feed?.close()
        feed = null
        screen.picture = null
        selected = null
    }

    override fun dispose() {
        disposed = true
        component.removeHierarchyListener(visibility)
        refresh.stop()
        closeView()
        actions.dispose()
        polling.dispose()
    }
}
