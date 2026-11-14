package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.IconButton
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.TitleButton
import com.intellij.icons.AllIcons
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.FlowLayout

/** Every simulator this Mac has, each with the one thing to do to it: start it, or stop it. */
@Composable
internal fun DeviceListPage(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    modifier: SwingModifier,
) {
    Panel(PanelLayout.Border(), modifier = modifier) {
        Panel(PanelLayout.Border(), modifier = SwingModifier.north().emptyBorder(JBUI.insets(0, 4, 0, 6))) {
            StatusLine(summary(controller), SwingModifier.center())
            Panel(PanelLayout.Flow(FlowLayout.RIGHT, 0, 0), modifier = SwingModifier.east()) {
                IconButton(AllIcons.Actions.Refresh, "Refresh the list", { controller.refreshDevices() })
            }
        }
        ScrollPane(modifier = SwingModifier.center().emptyBorder(JBUI.emptyInsets())) {
            Panel(PanelLayout.Border(), modifier = SwingModifier.viewport()) {
                Column(SwingModifier.north()) {
                    for (device in controller.devices) {
                        DeviceRow(device, controller, state)
                    }
                }
            }
        }
    }
}

@Composable
private fun DeviceRow(device: SimulatorDevice, controller: SimulatorWindowController, state: SimulatorPanelState) {
    val show = {
        state.listShown = false
        controller.open(device)
    }
    Panel(PanelLayout.Border(), modifier = SwingModifier.emptyBorder(JBUI.insets(1, 4, 1, 6))) {
        TitleButton(
            title = device.name,
            subtitle = "${device.runtime} · ${device.state}",
            onClick = { show() },
            modifier = SwingModifier.center(),
            chevron = false,
            icon = AllIcons.Actions.Preview,
        )
        Panel(PanelLayout.Flow(FlowLayout.RIGHT, 0, JBUI.scale(6)), modifier = SwingModifier.east()) {
            if (device.state == BOOTED) {
                IconButton(AllIcons.Actions.Suspend, "Shut down ${device.name}", { controller.shutdown(device) })
            } else {
                IconButton(AllIcons.Actions.Execute, "Boot ${device.name}", { show() })
            }
        }
    }
}

private fun summary(controller: SimulatorWindowController): String {
    val devices = controller.devices
    return when {
        controller.endpointFile.isEmpty() -> "Looking for simulators…"
        devices.isEmpty() -> "No simulators. Add one in Xcode: Window > Devices and Simulators."
        else -> "${devices.size} simulators · ${devices.count { it.state == BOOTED }} running"
    }
}
