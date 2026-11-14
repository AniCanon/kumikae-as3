package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.BarSeparator
import app.anicanon.kumikae.as3.ui.IconButton
import app.anicanon.kumikae.as3.ui.Native
import app.anicanon.kumikae.as3.ui.NativeScrollPane
import app.anicanon.kumikae.as3.ui.Pill
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.TitleButton
import com.intellij.icons.AllIcons
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.SplitPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.FlowLayout
import javax.swing.JSplitPane
import javax.swing.JTextArea

/** What the person has set in the panel itself: none of it is the Simulator's state. */
internal class SimulatorPanelState {
    var bundle by mutableStateOf(DEFAULT_BUNDLE)
    var consoleShown by mutableStateOf(false)
    var turns by mutableStateOf(0)

    private companion object {
        const val DEFAULT_BUNDLE = "org.kumikae.as3test"
    }
}

/** The device fills the panel; a heading names it, and a floating bar holds what is done to it. */
@Composable
internal fun SimulatorView(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    prompts: SimulatorPrompts,
    screen: SimulatorScreen,
    console: JTextArea,
) {
    // The picture and the console text are pushed into their native controls only when they change.
    LaunchedEffect(controller.frame) { screen.frame = controller.frame }
    LaunchedEffect(controller.inputEnabled) { screen.inputEnabled = controller.inputEnabled }
    LaunchedEffect(controller.selected, controller.status) { screen.placeholder = placeholder(controller) }
    LaunchedEffect(controller.console) {
        console.text = controller.console
        console.caretPosition = console.document.length
    }
    Panel(PanelLayout.Border()) {
        Header(controller, state, prompts, SwingModifier.north())
        SplitPane(
            modifier = SwingModifier.center().emptyBorder(JBUI.emptyInsets()),
            orientation = JSplitPane.VERTICAL_SPLIT,
            resizeWeight = SCREEN_SHARE,
        ) {
            Panel(PanelLayout.Border(), modifier = SwingModifier.first()) {
                Native(screen, SwingModifier.center())
                Panel(PanelLayout.Flow(FlowLayout.CENTER, 0, JBUI.scale(6)), modifier = SwingModifier.south()) {
                    DeviceBar(controller, state, prompts)
                }
            }
            NativeScrollPane(console, SwingModifier.second(), visible = state.consoleShown)
        }
        Panel(PanelLayout.Border(), modifier = SwingModifier.south()) {
            StatusLine(controller.status, SwingModifier.center())
            StatusLine(controller.capture, SwingModifier.east(), trailing = true)
        }
    }
}

@Composable
private fun Header(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    prompts: SimulatorPrompts,
    modifier: SwingModifier,
) {
    val device = controller.selected
    Panel(PanelLayout.Border(), modifier = modifier.emptyBorder(JBUI.insets(6, 4, 2, 6))) {
        TitleButton(
            title = device?.name ?: "Choose a simulator",
            subtitle = device?.let { "${it.runtime} · ${it.state}" } ?: "${controller.devices.size} available",
            onClick = { under -> prompts.choose(under, deviceEntries(controller)) },
            modifier = SwingModifier.west(),
        )
        Panel(PanelLayout.Flow(FlowLayout.RIGHT, JBUI.scale(2), JBUI.scale(4)), modifier = SwingModifier.east()) {
            IconButton(AllIcons.Actions.Execute, "Launch ${state.bundle}", { controller.launch(state.bundle) })
            IconButton(
                AllIcons.Debugger.Console,
                if (state.consoleShown) "Hide console" else "Show console",
                { state.consoleShown = !state.consoleShown },
                lit = state.consoleShown,
            )
            IconButton(AllIcons.Actions.More, "More", { under ->
                prompts.choose(under, moreEntries(controller, state, prompts))
            })
        }
    }
}

/** What is done to the running device. While it is not running, the one thing to do is start it. */
@Composable
private fun DeviceBar(controller: SimulatorWindowController, state: SimulatorPanelState, prompts: SimulatorPrompts) {
    val device = controller.selected
    if (device != null && device.state != BOOTED) {
        Button("Boot", onClick = { controller.boot() })
        return
    }
    val input = controller.inputEnabled
    val why = if (input) "" else " (needs idb)"
    Pill {
        IconButton(AllIcons.Nodes.HomeFolder, "Home$why", {
            controller.send(SimulatorInput.Press(HardwareButton.HOME))
        }, enabled = input)
        IconButton(AllIcons.Diff.Lock, "Lock$why", {
            controller.send(SimulatorInput.Press(HardwareButton.LOCK))
        }, enabled = input)
        IconButton(AllIcons.Actions.Refresh, "Rotate$why", {
            state.turns++
            controller.send(SimulatorInput.Rotate(Orientation.entries[state.turns % Orientation.entries.size]))
        }, enabled = input)
        BarSeparator()
        IconButton(
            AllIcons.Actions.Dump,
            "Copy screenshot",
            { controller.frame?.let { prompts.copy(it.image) } },
            enabled = controller.frame != null,
        )
        IconButton(
            AllIcons.Actions.Edit,
            "Type text$why",
            { prompts.textToType()?.let { controller.send(SimulatorInput.Text(it)) } },
            enabled = input,
        )
    }
}

internal fun deviceEntries(controller: SimulatorWindowController): List<MenuEntry> = controller.devices.map { device ->
    MenuEntry(device.label) { controller.selectDevice(device) }
} + MenuEntry("Refresh the list") { controller.refreshDevices() }

internal fun moreEntries(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    prompts: SimulatorPrompts,
): List<MenuEntry> = listOf(
    MenuEntry("App to launch… (${state.bundle})") { prompts.bundle(state.bundle)?.let { state.bundle = it } },
    MenuEntry("Install app…") { prompts.appToInstall()?.let(controller::install) },
    MenuEntry("Boot", enabled = controller.selected != null) { controller.boot() },
    MenuEntry("Refresh devices") { controller.refreshDevices() },
    MenuEntry("Copy agent endpoint path", enabled = controller.endpointFile.isNotEmpty()) {
        prompts.copy(controller.endpointFile)
    },
)

private fun placeholder(controller: SimulatorWindowController): Pair<String, String> {
    val device = controller.selected
    return when {
        device == null -> "No simulator chosen" to "Choose one from the list above"
        device.state != BOOTED -> "${device.name} is not running" to "Press Boot to start it"
        else -> "Waiting for the screen" to controller.notice
    }
}

private const val BOOTED = "Booted"
private const val SCREEN_SHARE = 0.72
