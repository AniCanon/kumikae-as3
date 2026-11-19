package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.TitleButton
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.Disposable
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.event.HierarchyEvent
import java.awt.event.HierarchyListener

internal class GameDeviceCatalog(
    private val ios: SimulatorRepository?,
    private val android: AndroidEmulators,
    private val openIos: (SimulatorDevice) -> Unit,
    private val openAndroid: (AndroidAvd) -> Unit,
) : Disposable {
    private val iosWorker = BackgroundWorker("iOS simulator list")
    private val androidWorker = BackgroundWorker("Android emulator list")
    private var simulators by mutableStateOf(emptyList<SimulatorDevice>())
    private var emulators by mutableStateOf(emptyList<AndroidAvd>())
    private var iosStatus by mutableStateOf("Loading simulators…")
    private var graphics by mutableStateOf(android.graphics)
    private var androidStatus by mutableStateOf("Loading emulators…")

    val component = composePanel(this) {
        ScrollPane {
            Panel(PanelLayout.Border(), modifier = SwingModifier.viewport()) {
                Column(SwingModifier.north().emptyBorder(JBUI.insets(12))) {
                    if (ios != null) {
                        Label("iOS Simulators")
                        simulators.forEach { device ->
                            TitleButton(device.name, "${device.runtime} · ${device.state}", { openIos(device) })
                        }
                        if (iosStatus.isNotBlank()) StatusLine(iosStatus)
                    }
                    Label("Android Emulators", SwingModifier.emptyBorder(JBUI.insetsTop(12)))
                    emulators.forEach { device ->
                        TitleButton(
                            device.name.replace('_', ' '),
                            if (device.serial == null) "Stopped" else "Running",
                            { openAndroid(device) },
                        )
                    }
                    if (emulators.any { it.serial == null }) {
                        Label("Graphics for next emulator start")
                        ComboBox(
                            AndroidGraphics.entries,
                            graphics,
                            onSelectionChange = { value ->
                                if (value != null) {
                                    graphics = value
                                    android.graphics = value
                                }
                            },
                        ) { Label(it.label) }
                    }
                    if (androidStatus.isNotBlank()) StatusLine(androidStatus)
                    Button("Refresh devices", ::refresh)
                }
            }
        }
    }

    private var showing = false
    private val visibility = HierarchyListener { event ->
        if ((event.changeFlags and HierarchyEvent.SHOWING_CHANGED.toLong()) == 0L) return@HierarchyListener
        val now = component.isShowing
        if (now && !showing) refresh()
        showing = now
    }

    init {
        component.addHierarchyListener(visibility)
    }

    private fun refresh() {
        ios?.let { repository ->
            iosWorker.submit({
                repository.connect()
                repository.devices()
            }) { result ->
                result.onSuccess { simulators = it }.onFailure { simulators = emptyList() }
                iosStatus =
                    result.exceptionOrNull()?.message ?: if (simulators.isEmpty()) "No installed simulators" else ""
            }
        }
        androidWorker.submit(android::list) { result ->
            result.onSuccess { emulators = it }.onFailure { emulators = emptyList() }
            androidStatus =
                result.exceptionOrNull()?.message ?: if (emulators.isEmpty()) "No installed emulators" else ""
        }
    }

    override fun dispose() {
        component.removeHierarchyListener(visibility)
        iosWorker.dispose()
        androidWorker.dispose()
    }
}
