package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import app.anicanon.kumikae.as3.ui.BarSeparator
import app.anicanon.kumikae.as3.ui.IconButton
import app.anicanon.kumikae.as3.ui.Native
import app.anicanon.kumikae.as3.ui.NativeScrollPane
import app.anicanon.kumikae.as3.ui.Pill
import app.anicanon.kumikae.as3.ui.StatusLine
import com.intellij.icons.AllIcons
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.FlowLayout
import javax.swing.JTextArea

/** One device: a bar of what the panel does, the device itself, and a floating bar of what is done to it. */
@Composable
internal fun DevicePage(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    prompts: SimulatorPrompts,
    screen: SimulatorScreen,
    console: JTextArea,
    device: SimulatorDevice,
    modifier: SwingModifier,
) {
    // The picture and the console text are pushed into their native controls only when they change.
    LaunchedEffect(controller.frame) { screen.frame = controller.frame }
    // Read here, not inside the effect: only what composition reads brings the page back when it changes.
    val input = controller.inputEnabled
    val waiting = placeholder(device, controller)
    val zoom = state.zoom
    SideEffect {
        screen.inputEnabled = input
        screen.deviceName = device.name
        screen.placeholder = waiting
        screen.zoom = zoom
        screen.onZoom = { state.zoom = it }
        screen.onInputWanted = { wantInput(controller, prompts) }
    }
    LaunchedEffect(controller.console) {
        console.text = controller.console
        console.caretPosition = console.document.length
    }
    Panel(PanelLayout.Border(), modifier = modifier) {
        ToolBar(controller, state, prompts, device, SwingModifier.north())
        Panel(PanelLayout.Border(), modifier = SwingModifier.center()) {
            ScrollPane(modifier = SwingModifier.center().emptyBorder(JBUI.emptyInsets())) {
                Native(screen, SwingModifier.viewport())
            }
            Panel(PanelLayout.Flow(FlowLayout.CENTER, 0, JBUI.scale(6)), modifier = SwingModifier.south()) {
                DeviceBar(controller, state, prompts, device)
            }
        }
        // A drawer, not a split: closed, it takes no room and leaves no divider behind.
        if (state.consoleShown) {
            NativeScrollPane(console, SwingModifier.south(), height = JBUI.scale(CONSOLE_HEIGHT))
        }
    }
}

@Composable
private fun ToolBar(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    prompts: SimulatorPrompts,
    device: SimulatorDevice,
    modifier: SwingModifier,
) {
    Panel(PanelLayout.Border(), modifier = modifier.emptyBorder(JBUI.insets(0, 4, 0, 6))) {
        StatusLine("${device.runtime} · ${device.state}", SwingModifier.center())
        Panel(PanelLayout.Flow(FlowLayout.RIGHT, JBUI.scale(2), 0), modifier = SwingModifier.east()) {
            IconButton(AllIcons.Actions.Execute, "Launch ${state.bundle}", { controller.launch(state.bundle) })
            IconButton(
                AllIcons.Debugger.Console,
                if (state.consoleShown) "Hide console" else "Show console",
                { state.consoleShown = !state.consoleShown },
                lit = state.consoleShown,
            )
            BarSeparator()
            IconButton(
                AllIcons.General.ZoomOut,
                "Zoom out",
                { state.zoom = step(state.zoom, -1) },
                enabled =
                state.zoom > 1.0,
            )
            IconButton(AllIcons.General.FitContent, "Fit to the panel", { state.zoom = 1.0 }, lit = state.zoom == 1.0)
            IconButton(
                AllIcons.General.ZoomIn,
                "Zoom in",
                { state.zoom = step(state.zoom, 1) },
                enabled =
                state.zoom < SimulatorScreen.MAX_ZOOM,
            )
            BarSeparator()
            IconButton(AllIcons.Actions.More, "More", { under ->
                prompts.choose(under, moreEntries(controller, state, prompts))
            })
        }
    }
}

/** What is done to the running device. While it is not running, the one thing to do is start it. */
@Composable
private fun DeviceBar(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    prompts: SimulatorPrompts,
    device: SimulatorDevice,
) {
    if (device.state != BOOTED) {
        if (!controller.working) Button("Boot", onClick = { controller.boot() })
        return
    }
    // Always pressable: while input is off, a press asks to switch it on instead of doing nothing.
    val send: (SimulatorInput) -> Unit = { what ->
        if (controller.inputEnabled) controller.send(what) else wantInput(controller, prompts)
    }
    Pill {
        IconButton(AllIcons.Nodes.HomeFolder, "Home", { send(SimulatorInput.Press(HardwareButton.HOME)) })
        IconButton(AllIcons.Diff.Lock, "Lock", { send(SimulatorInput.Press(HardwareButton.LOCK)) })
        IconButton(AllIcons.Actions.Refresh, "Rotate", {
            if (controller.inputEnabled) state.turns++
            send(SimulatorInput.Rotate(Orientation.entries[state.turns % Orientation.entries.size]))
        })
        BarSeparator()
        IconButton(
            AllIcons.Actions.Dump,
            "Copy screenshot",
            { controller.frame?.let { prompts.copy(it.image) } },
            enabled = controller.frame != null,
        )
        IconButton(AllIcons.Actions.Edit, "Type text", {
            if (controller.inputEnabled) {
                prompts.textToType()?.let { controller.send(SimulatorInput.Text(it)) }
            } else {
                wantInput(controller, prompts)
            }
        })
    }
}

internal fun moreEntries(
    controller: SimulatorWindowController,
    state: SimulatorPanelState,
    prompts: SimulatorPrompts,
): List<MenuEntry> = listOfNotNull(
    MenuEntry("App to launch… (${state.bundle})") { prompts.bundle(state.bundle)?.let { state.bundle = it } },
    MenuEntry("Install app…") { prompts.appToInstall()?.let(controller::install) },
    MenuEntry(
        if (controller.inputEnabled) "Input is on (idb)" else "Switch input on…",
        enabled = !controller.inputEnabled,
    ) {
        if (prompts.confirmInput()) controller.enableInput()
    },
    MenuEntry("Install idb…") {
        if (prompts.confirmInstall()) {
            state.consoleShown = true
            controller.installIdb()
        }
    }.takeIf { controller.idbMissing },
    MenuEntry("Copy agent endpoint path", enabled = controller.endpointFile.isNotEmpty()) {
        prompts.copy(controller.endpointFile)
    },
)

private fun placeholder(device: SimulatorDevice, controller: SimulatorWindowController): Pair<String, String> = when {
    device.state == BOOTED -> "Waiting for the screen" to controller.notice
    controller.working -> "Starting ${device.name}…" to "This can take a minute"
    else -> "${device.name} is not running" to "Press Boot to start it"
}

/** The next zoom a button goes to. Close to fitting snaps to fitting, so that the scroll bars go away. */
private fun step(zoom: Double, by: Int): Double {
    val next = if (by > 0) zoom * ZOOM_STEP else zoom / ZOOM_STEP
    return if (next < 1.0 + ZOOM_SNAP) 1.0 else next.coerceAtMost(SimulatorScreen.MAX_ZOOM)
}

/** Input was asked for while it is off: the owner is asked once, and a yes switches it on. */
private fun wantInput(controller: SimulatorWindowController, prompts: SimulatorPrompts) {
    if (prompts.confirmInput()) controller.enableInput()
}

private const val ZOOM_STEP = 1.25
private const val ZOOM_SNAP = 0.02
private const val CONSOLE_HEIGHT = 190
