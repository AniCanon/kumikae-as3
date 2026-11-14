package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.Tab
import com.intellij.icons.AllIcons
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.FlowLayout
import javax.swing.JTextArea

/** What the person has set in the panel itself: none of it is the Simulator's state. */
internal class SimulatorPanelState {
    var consoleShown by mutableStateOf(false)
    var turns by mutableStateOf(0)

    /** The list of simulators is in front, not a device. */
    var listShown by mutableStateOf(true)
    var zoom by mutableStateOf(1.0)
}

/** A tab for the list of simulators and one for each that is running; under them, whichever is chosen. */
@Composable
internal fun SimulatorView(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    prompts: SimulatorPrompts,
    screen: SimulatorScreen,
    console: JTextArea,
) {
    val device = controller.selected
    val open = openDevices(controller)
    val list = state.listShown || device == null || open.none { it.id == device.id }
    Panel(PanelLayout.Border()) {
        Tabs(controller, state, open, list, SwingModifier.north())
        val shown = device.takeUnless { list }
        if (shown == null) {
            DeviceListPage(controller, state, SwingModifier.center())
        } else {
            DevicePage(controller, state, prompts, screen, console, shown, SwingModifier.center())
        }
        Panel(PanelLayout.Border(), modifier = SwingModifier.south()) {
            StatusLine(controller.status, SwingModifier.center())
            StatusLine(if (list) "" else controller.capture, SwingModifier.east(), trailing = true)
        }
    }
}

/** The devices with a tab: those running, and the one that is being started. */
private fun openDevices(controller: SimulatorWindowController): List<SimulatorDevice> =
    (controller.running + listOfNotNull(controller.selected.takeIf { controller.working })).distinctBy { it.id }

@Composable
private fun Tabs(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    open: List<SimulatorDevice>,
    list: Boolean,
    modifier: SwingModifier,
) {
    Panel(
        PanelLayout.Flow(FlowLayout.LEFT, JBUI.scale(4), JBUI.scale(6)),
        modifier = modifier.emptyBorder(JBUI.insets(0, 4)),
    ) {
        Tab("Simulators", chosen = list, onClick = { state.listShown = true }, icon = AllIcons.Nodes.Folder)
        for (device in open) {
            Tab(
                device.name,
                chosen = !list && device.id == controller.selected?.id,
                onClick = {
                    state.listShown = false
                    controller.open(device)
                },
                onClose = { controller.shutdown(device) }.takeIf { device.state == BOOTED },
            )
        }
    }
}

internal const val BOOTED = "Booted"
