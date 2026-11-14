package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.FormRow
import app.anicanon.kumikae.as3.ui.Native
import app.anicanon.kumikae.as3.ui.NativeScrollPane
import app.anicanon.kumikae.as3.ui.Note
import app.anicanon.kumikae.as3.ui.ReadOnlyField
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.SplitPane
import org.jetbrains.compose.swing.components.text.TextField
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.interaction.enabled
import java.awt.FlowLayout
import javax.swing.JSplitPane
import javax.swing.JTextArea

private data class DeviceChoice(val device: SimulatorDevice) {
    override fun toString() = device.label
}

private data class OrientationChoice(val orientation: Orientation, val label: String) {
    override fun toString() = label
}

private val orientations = listOf(
    OrientationChoice(Orientation.PORTRAIT, "Portrait"),
    OrientationChoice(Orientation.LANDSCAPE_LEFT, "Landscape left"),
    OrientationChoice(Orientation.LANDSCAPE_RIGHT, "Landscape right"),
    OrientationChoice(Orientation.PORTRAIT_UPSIDE_DOWN, "Upside down"),
)

/** Renders controller state and forwards user actions. Draft form values live here; nothing else does. */
@Composable
internal fun SimulatorView(controller: SimulatorWindowController, screen: SimulatorScreen, console: JTextArea) {
    // The picture and the console text are pushed into their native controls only when they change.
    LaunchedEffect(controller.frame) { screen.frame = controller.frame }
    LaunchedEffect(controller.inputEnabled) { screen.inputEnabled = controller.inputEnabled }
    LaunchedEffect(controller.console) {
        console.text = controller.console
        console.caretPosition = console.document.length
    }
    Panel(PanelLayout.Border(0, JBUI.scale(6)), modifier = SwingModifier.emptyBorder(JBUI.insets(8))) {
        Column(SwingModifier.north()) {
            DeviceRow(controller)
            AppRows(controller)
            InputRows(controller)
            Note(controller.notice)
            Note(controller.status, secondary = false)
        }
        SplitPane(
            modifier = SwingModifier.center(),
            orientation = JSplitPane.VERTICAL_SPLIT,
            resizeWeight = SCREEN_SHARE,
        ) {
            Panel(PanelLayout.Border(), modifier = SwingModifier.first()) {
                Native(screen, SwingModifier.center())
                Label(controller.capture.ifEmpty { "No picture yet." }, SwingModifier.south())
            }
            NativeScrollPane(console, SwingModifier.second())
        }
        Panel(PanelLayout.Border(0, JBUI.scale(2)), modifier = SwingModifier.south()) {
            Label("Agent endpoint file", SwingModifier.north())
            ReadOnlyField(controller.endpointFile, SwingModifier.center())
        }
    }
}

@Composable
private fun DeviceRow(controller: SimulatorWindowController) {
    FormRow("Device", action = {
        Panel(PanelLayout.Flow(FlowLayout.LEFT, JBUI.scale(4), 0)) {
            Button("Refresh", onClick = { controller.refreshDevices() })
            Button("Boot", onClick = { controller.boot() })
        }
    }) { place ->
        ComboBox(
            controller.devices.map(::DeviceChoice),
            controller.selected?.let(::DeviceChoice),
            onSelectionChange = { choice -> choice?.let { controller.selectDevice(it.device) } },
            modifier = place,
        )
    }
}

@Composable
private fun AppRows(controller: SimulatorWindowController) {
    var appPath by remember { mutableStateOf("") }
    var bundle by remember { mutableStateOf(DEFAULT_BUNDLE) }
    FormRow("App", action = { Button("Install", onClick = { controller.install(appPath) }) }) { place ->
        TextField(appPath, { appPath = it }, modifier = place, columns = FIELD_COLUMNS)
    }
    FormRow("Bundle", action = { Button("Launch", onClick = { controller.launch(bundle) }) }) { place ->
        TextField(bundle, { bundle = it }, modifier = place, columns = FIELD_COLUMNS)
    }
}

@Composable
private fun InputRows(controller: SimulatorWindowController) {
    var text by remember { mutableStateOf("") }
    var orientation by remember { mutableStateOf(orientations.first()) }
    val allowed = SwingModifier.enabled(controller.inputEnabled)
    FormRow("Text", action = {
        Button("Type", onClick = { controller.send(SimulatorInput.Text(text)) }, modifier = allowed)
    }) { place ->
        TextField(text, { text = it }, modifier = place.enabled(controller.inputEnabled), columns = FIELD_COLUMNS)
    }
    FormRow("Rotate", action = {
        Button("Rotate", onClick = {
            controller.send(SimulatorInput.Rotate(orientation.orientation))
        }, modifier = allowed)
    }) { place ->
        ComboBox(
            orientations,
            orientation,
            onSelectionChange = { choice -> choice?.let { orientation = it } },
            modifier = place.enabled(controller.inputEnabled),
        )
    }
    Panel(PanelLayout.Flow(FlowLayout.LEFT, JBUI.scale(4), 0)) {
        Button("Home", onClick = { controller.send(SimulatorInput.Press(HardwareButton.HOME)) }, modifier = allowed)
        Button("Lock", onClick = { controller.send(SimulatorInput.Press(HardwareButton.LOCK)) }, modifier = allowed)
    }
}

private const val DEFAULT_BUNDLE = "org.kumikae.as3test"
private const val FIELD_COLUMNS = 12
private const val SCREEN_SHARE = 0.75
