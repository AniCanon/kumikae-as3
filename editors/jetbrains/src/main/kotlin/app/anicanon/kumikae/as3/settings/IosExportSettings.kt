package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.IconButton
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import com.intellij.icons.AllIcons
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.interaction.enabled
import java.awt.FlowLayout

private data class TeamChoice(val id: String?, val label: String) {
    override fun toString() = label
}

@Composable
internal fun IosExecutionSettings(value: ProjectSettings, controller: SettingsController) {
    SettingsSection("AS3 execution") {
        ComboBox(IosExecutionMode.entries.toList(), value.executionMode, onSelectionChange = { mode ->
            if (mode != null) controller.edit { it.copy(executionMode = mode) }
        }) { Label(it.label) }
        StatusLine(value.executionMode.detail)
    }
}

@Composable
internal fun IosExportSettings(value: ProjectSettings, controller: SettingsController) {
    SettingsSection("Device signing team") {
        var manual by remember(value.preset) { mutableStateOf(false) }
        val none = TeamChoice("", "Choose a team…")
        val custom = TeamChoice(null, "Enter team ID…")
        val teams = controller.teams.map { TeamChoice(it.id, it.label) }
        val saved = teams.find { it.id == value.signingTeam } ?: when {
            value.signingTeam.isEmpty() -> none
            manual -> custom
            else -> TeamChoice(value.signingTeam, "Saved team · ${value.signingTeam}")
        }
        val selected = if (manual) custom else saved
        val choices = (listOf(none) + teams + listOf(saved, custom)).distinct()
        Panel(PanelLayout.Border(8, 0)) {
            ComboBox(choices, selected, modifier = SwingModifier.center(), onSelectionChange = { choice ->
                if (choice != null) {
                    manual = choice == custom
                    choice.id?.let { id -> controller.edit { it.copy(signingTeam = id) } }
                }
            }) { Label(it.label) }
            if (controller.hasXcodeTeams) {
                Panel(PanelLayout.Flow(FlowLayout.LEFT, 2, 0), modifier = SwingModifier.east()) {
                    IconButton(AllIcons.Actions.Refresh, "Refresh signing teams", {
                        controller.refreshTeams()
                    }, enabled = !controller.teamsBusy)
                    Button("Open Xcode", controller::openXcode, SwingModifier.enabled(!controller.teamsBusy))
                }
            }
        }
        if (manual) {
            LabelledField(
                "Apple team ID",
                value.signingTeam,
                { text -> controller.edit { it.copy(signingTeam = text) } },
                placeholder = "10-character team ID",
            )
        }
        if (controller.teamStatus.isNotEmpty()) StatusLine(controller.teamStatus)
        StatusLine("Simulator runs work without a signing team.")
    }
}
