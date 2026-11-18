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
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder

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
                    if (androidStatus.isNotBlank()) StatusLine(androidStatus)
                    Button("Refresh devices", ::refresh)
                }
            }
        }
    }

    init {
        component.addHierarchyListener { if (component.isShowing) refresh() }
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
        iosWorker.dispose()
        androidWorker.dispose()
    }
}
