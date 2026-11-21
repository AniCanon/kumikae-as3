package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.interaction.enabled

internal sealed interface BuildTargetChoice {
    val label: String
}

private data class CompanionTarget(override val label: String) : BuildTargetChoice {
    override fun toString() = label
}

@Composable
internal fun BuildTargetPicker(
    value: ProjectSettings,
    controller: SettingsController,
    addTarget: (() -> Unit)?,
    targetSettings: Map<String, () -> Unit>,
    presetSettings: @Composable () -> Unit,
) {
    var companion by remember { mutableStateOf<CompanionTarget?>(null) }
    val choices = value.presets + targetSettings.keys.map(::CompanionTarget)
    val selected = companion?.takeIf { it in choices }
        ?: value.presets.find { it.section == value.preset } ?: choices.firstOrNull()
    SettingsSection("Build target") {
        Panel(PanelLayout.Border(8, 0)) {
            if (selected == null) {
                StatusLine("Add a target to configure its build and signing settings.", SwingModifier.center())
            } else {
                ComboBox(choices, selected, modifier = SwingModifier.center(), onSelectionChange = { target ->
                    when (target) {
                        is CompanionTarget -> companion = target

                        is ExportPreset -> {
                            companion = null
                            if (target.section != value.preset) controller.edit { it.select(target.section) }
                        }

                        null -> Unit
                    }
                }) { Label(it.label) }
            }
            if (addTarget != null) Button("Add target…", addTarget, SwingModifier.east().enabled(!controller.dirty))
        }
        if (selected is CompanionTarget) {
            SettingsActions {
                Button(
                    "${selected.label} settings…",
                    targetSettings.getValue(selected.label),
                    SwingModifier.enabled(!controller.dirty),
                )
            }
        }
    }
    if (selected is ExportPreset) presetSettings()
}
