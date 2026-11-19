package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.SurfaceGroup
import com.intellij.icons.AllIcons
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import org.jetbrains.compose.swing.modifier.appearance.foreground
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.Graphics
import javax.swing.Icon
import javax.swing.JButton

@Composable
internal fun DeviceCatalogView(
    ios: List<SimulatorDevice>?,
    android: List<AndroidAvd>,
    iosStatus: String,
    androidStatus: String,
    refreshing: Boolean,
    graphics: AndroidGraphics,
    openIos: (SimulatorDevice) -> Unit,
    openAndroid: (AndroidAvd) -> Unit,
    setGraphics: (AndroidGraphics) -> Unit,
    refresh: () -> Unit,
) {
    val count = ios.orEmpty().size + android.size
    val active = ios.orEmpty().count { it.state == BOOTED } + android.count { it.serial != null }
    val summary = when {
        refreshing -> "Refreshing devices…"
        count == 1 -> "1 device"
        else -> "$count devices"
    }
    Panel(PanelLayout.Border(), modifier = SwingModifier.emptyBorder(JBUI.insets(8, 12))) {
        Panel(PanelLayout.Border(), modifier = SwingModifier.north().emptyBorder(JBUI.insets(0, 4, 6, 0))) {
            Panel(PanelLayout.Border(JBUI.scale(8), 0), modifier = SwingModifier.west()) {
                Label(summary, SwingModifier.west().font(JBFont.h2().asBold()))
                if (active > 0) {
                    Label("·  $active running", SwingModifier.center().foreground(catalogSecondary))
                }
            }
            ToolbarAction("Refresh devices", AllIcons.Actions.Refresh, !refreshing, refresh, SwingModifier.east())
        }
        ScrollPane(modifier = SwingModifier.center().emptyBorder(JBUI.emptyInsets())) {
            Panel(PanelLayout.Border(), modifier = SwingModifier.viewport()) {
                Column(SwingModifier.north()) {
                    if (ios != null) {
                        CatalogSection("iOS simulators")
                        SurfaceGroup {
                            for (device in ios.sortedByDescending { it.state == BOOTED }) {
                                key(device.id) {
                                    val state = when (device.state) {
                                        BOOTED -> "Running"
                                        "Shutdown" -> "Stopped"
                                        else -> device.state
                                    }
                                    CatalogRow(
                                        device.name,
                                        "${device.runtime} · $state",
                                        device.state == BOOTED,
                                        android = false,
                                        available = state == "Running" || state == "Stopped",
                                    ) {
                                        openIos(device)
                                    }
                                }
                            }
                            if (iosStatus.isNotBlank()) StatusLine(iosStatus)
                        }
                    }
                    var configure by remember { mutableStateOf(false) }
                    val canConfigure = android.any { it.serial == null }
                    CatalogSection("Android emulators") {
                        if (canConfigure) {
                            ToolbarAction("Emulator graphics", AllIcons.General.Settings, true, {
                                configure = !configure
                            })
                        }
                    }
                    SurfaceGroup {
                        for (device in android.sortedByDescending { it.serial != null }) {
                            key(device.name, device.serial) {
                                val running = device.serial != null
                                CatalogRow(
                                    device.name.replace('_', ' '),
                                    if (running) "Running" else "Stopped",
                                    running,
                                    android = true,
                                ) {
                                    openAndroid(device)
                                }
                            }
                        }
                        if (androidStatus.isNotBlank()) StatusLine(androidStatus)
                    }
                    if (configure && canConfigure) {
                        Column(SwingModifier.emptyBorder(JBUI.insets(6, 4))) {
                            Label("Graphics for next start", SwingModifier.font(JBFont.small()))
                            ComboBox(AndroidGraphics.entries, graphics, onSelectionChange = {
                                it?.let(setGraphics)
                            }) { Label(it.label) }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun CatalogSection(title: String, action: @Composable () -> Unit = {}) {
    Panel(PanelLayout.Border(), modifier = SwingModifier.emptyBorder(JBUI.insets(8, 4, 4, 0))) {
        Label(
            title,
            SwingModifier.center().font(JBFont.label().asBold()).foreground(catalogSecondary)
                .emptyBorder(JBUI.insets(6, 0)),
        )
        Panel(PanelLayout.Border(), modifier = SwingModifier.east()) { action() }
    }
}

@Composable
private fun ToolbarAction(
    name: String,
    icon: Icon,
    enabled: Boolean,
    click: () -> Unit,
    modifier: SwingModifier = SwingModifier,
) {
    SwingNode(factory = {
        object : JButton(icon) {
            override fun isContentAreaFilled() = hasFocus() || model?.isRollover == true || model?.isPressed == true
            override fun paintBorder(g: Graphics) {
                if (hasFocus()) super.paintBorder(g)
            }
        }.apply {
            preferredSize = JBUI.size(26)
            isRolloverEnabled = true
            margin = JBUI.emptyInsets()
            toolTipText = name
            accessibleContext.accessibleName = name
        }
    }, modifier = modifier, update = {
        set(enabled) { isEnabled = it }
        set(click) {
            actionListeners.forEach(::removeActionListener)
            addActionListener { _ -> it() }
        }
    })
}

@Composable
private fun CatalogRow(
    title: String,
    detail: String,
    running: Boolean,
    android: Boolean,
    available: Boolean = true,
    click: () -> Unit,
) {
    SwingNode(factory = { CatalogDeviceButton() }, update = {
        set(CatalogDevice(title, detail, running, android, available)) { device = it }
        set(click) { activate = it }
    })
}
