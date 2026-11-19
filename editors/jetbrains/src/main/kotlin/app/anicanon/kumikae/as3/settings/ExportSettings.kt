package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import com.intellij.ui.components.ActionLink
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.interaction.enabled
import org.jetbrains.compose.swing.node.SwingNode

@Composable
internal fun ExportSettings(
    value: ProjectSettings,
    controller: SettingsController,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
    addTarget: (() -> Unit)?,
    release: ((ProjectSettings, ExportPreset) -> Unit)?,
    configureKey: ((ExportPreset) -> Unit)?,
    hasKey: (ExportPreset) -> Boolean,
    openStore: (String) -> Unit,
    distribute: ((ProjectSettings, ExportPreset) -> Unit)?,
    deliveryStatus: (ExportPreset) -> String,
) {
    SettingsSection("Build target") {
        Panel(PanelLayout.Border(8, 0)) {
            if (value.presets.isEmpty()) {
                StatusLine("Add a target to configure its build and signing settings.", SwingModifier.center())
            } else {
                val selected = value.presets.firstOrNull { it.section == value.preset }
                ComboBox(value.presets, selected, modifier = SwingModifier.center(), onSelectionChange = { preset ->
                    if (preset != null) controller.edit { it.select(preset.section) }
                }) { Label(if (it.name == it.platform) it.name else it.toString()) }
            }
            if (addTarget != null) {
                Button("Add target…", addTarget, SwingModifier.east().enabled(!controller.dirty))
            }
        }
    }
    val preset = value.presets.firstOrNull { it.section == value.preset } ?: return
    if (preset.identifierKey == null) return
    val ios = preset.platform == "iOS"
    SettingsSection("Application identity") {
        LabelledField(if (ios) "Bundle identifier" else "Package name", value.identifier, { text ->
            controller.edit { it.copy(identifier = text) }
        })
        Panel(PanelLayout.Grid(1, 2, JBUI.scale(12), 0)) {
            LabelledField(
                "Release version",
                value.releaseVersion,
                { text ->
                    controller.edit { it.copy(releaseVersion = text) }
                },
                placeholder = value.version.ifBlank { "Use project version" },
            )
            LabelledField(if (ios) "Build number" else "Version code", value.buildNumber, { text ->
                controller.edit { it.copy(buildNumber = text) }
            }, placeholder = if (ios) "Use project version" else "1")
        }
    }
    if (ios) {
        IosExportSettings(value, controller)
    } else {
        SettingsSection("Android signing") {
            StatusLine(
                if (hasKey(preset)) {
                    "Rider has an upload key for this preset."
                } else {
                    "Device runs use debug signing. Choose an upload key here or use your Godot signing settings."
                },
            )
            if (configureKey != null) {
                SettingsActions(0) {
                    Button(if (hasKey(preset)) "Manage upload key…" else "Configure upload key…", {
                        configureKey(preset)
                    })
                }
            }
        }
    }
    SettingsSection(if (ios) "TestFlight & App Store" else "Google Play") {
        val personal = ios && controller.teams.any { it.id == value.signingTeam && it.personal }
        StatusLine(
            when {
                personal -> "Choose an Apple Developer Program team to distribute this app."
                ios -> "Archive and send this build to an internal TestFlight group."
                else -> "Send a signed app bundle to your Google Play internal testers."
            },
        )
        SettingsActions(4) {
            fun prepare(action: (ProjectSettings, ExportPreset) -> Unit) {
                if (controller.dirty) controller.apply()
                if (!controller.dirty &&
                    controller.readyToExport(listOf(preset))
                ) {
                    controller.draft?.let { action(it, preset) }
                }
            }
            if (distribute != null) {
                Button(if (controller.dirty) "Apply & distribute to testers…" else "Distribute to testers…", {
                    prepare(distribute)
                }, SwingModifier.enabled(!personal))
            }
            if (release != null) {
                val action = if (ios) "Prepare Xcode project…" else "Build Play bundle…"
                Button(if (controller.dirty) "Apply & " + action.replaceFirstChar(Char::lowercase) else action, {
                    prepare(release)
                }, SwingModifier.enabled(!personal))
            }
            val open by rememberUpdatedState { openStore(preset.platform) }
            SwingNode(factory = {
                ActionLink().apply {
                    setExternalLinkIcon()
                    addActionListener { open() }
                }
            }, update = { set(ios) { text = if (it) "App Store Connect" else "Play Console" } })
        }
    }
    deliveryStatus(preset).takeIf { it.isNotBlank() }?.let { StatusLine(it) }
    if (ios) IosExecutionSettings(value, controller)
    var artwork by remember(value.preset) { mutableStateOf(false) }
    SettingsActions {
        Button(if (artwork) "Hide platform icon" else "Platform icon…", { artwork = !artwork })
    }
    if (artwork) {
        ArtworkField(GameResourceKind.ExportIcon, value.exportIcon, controller.exportPreview, choose) { path ->
            controller.edit { it.copy(exportIcon = path) }
        }
    }
}
