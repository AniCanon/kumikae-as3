package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.Tab
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.interaction.enabled
import java.awt.FlowLayout

@Composable
internal fun ProjectSettingsView(
    controller: SettingsController,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
    modifier: SwingModifier = SwingModifier,
    initialPage: String = "General",
    addTarget: (() -> Unit)? = null,
    targetSettings: Map<String, () -> Unit> = emptyMap(),
    export: ((ProjectSettings, List<ExportPreset>) -> Unit)? = null,
    release: ((ProjectSettings, ExportPreset) -> Unit)? = null,
    configureKey: ((ExportPreset) -> Unit)? = null,
    hasKey: (ExportPreset) -> Boolean = { false },
    openStore: (String) -> Unit = {},
    distribute: ((ProjectSettings, ExportPreset) -> Unit)? = null,
    deliveryStatus: (ExportPreset) -> String = { "" },
) {
    var page by remember { mutableStateOf(initialPage) }
    SettingsLayout(controller.draft?.name.orEmpty(), page, { page = it }, modifier, footer = {
        Column {
            if (controller.status.isNotBlank()) StatusLine(controller.status)
            Panel(PanelLayout.Flow(FlowLayout.RIGHT, 8, 8)) {
                Button("Reload", { controller.reload() })
                Button("Apply", { controller.apply() }, SwingModifier.enabled(controller.dirty))
            }
        }
    }) { compact ->
        Column {
            val draft = controller.draft
            if (draft != null) {
                when (page) {
                    "General" -> GeneralSettings(draft, controller, choose, compact)

                    "Display" -> DisplaySettings(draft, controller)

                    "Properties" -> GodotProperties(draft, controller)

                    "Export" -> {
                        BuildTargetPicker(draft, controller, addTarget, targetSettings) {
                            ExportSettings(
                                draft,
                                controller,
                                choose,
                                release,
                                configureKey,
                                hasKey,
                                openStore,
                                distribute,
                                deliveryStatus,
                            )
                            if (export != null && draft.presets.isNotEmpty()) {
                                SettingsActions {
                                    draft.presets.find { it.section == draft.preset }?.let { selected ->
                                        Button(
                                            "Export ${selected.name}…",
                                            {
                                                val presets = listOf(selected)
                                                if (controller.readyToExport(presets)) export(draft, presets)
                                            },
                                            SwingModifier.enabled(!controller.dirty),
                                        )
                                    }
                                    if (draft.presets.size >
                                        1
                                    ) {
                                        Button(
                                            "Export All…",
                                            {
                                                if (controller.readyToExport(draft.presets)) {
                                                    export(draft, draft.presets)
                                                }
                                            },
                                            SwingModifier.enabled(!controller.dirty),
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun GeneralSettings(
    value: ProjectSettings,
    controller: SettingsController,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
    compact: Boolean,
) {
    SettingsSection("Application") {
        LabelledField("Application name", value.name, { text -> controller.edit { it.copy(name = text) } })
        LabelledField("Version", value.version, { text -> controller.edit { it.copy(version = text) } })
        Label("Main scene", SwingModifier.emptyBorder(JBUI.insetsTop(12)))
        val choices = controller.sceneLabels
        val scenes = (
            listOf(value.mainScene) +
                choices.keys.filter { it.startsWith("res://") && choices[it] != choices[value.mainScene] }.sorted()
            ).distinct()
        ComboBox(scenes, value.mainScene, onSelectionChange = { selected ->
            if (selected != null) controller.edit { it.copy(mainScene = selected) }
        }) { Label(choices[it] ?: it.removePrefix("res://").ifEmpty { "Choose a scene" }) }
    }
    SettingsSection("Artwork") {
        Panel(PanelLayout.Border(16, 8)) {
            Column(if (compact) SwingModifier.north() else SwingModifier.west()) {
                ArtworkField(GameResourceKind.Icon, value.icon, controller.preview, choose) { path ->
                    controller.edit { it.copy(icon = path) }
                }
            }
            Column(SwingModifier.center()) {
                val layout = remember(value.projectText, value.width, value.height) { SplashLayout.read(value) }
                ArtworkField(GameResourceKind.Splash, value.splash, controller.splashPreview, choose, layout) { path ->
                    controller.edit { it.copy(splash = path) }
                }
            }
        }
    }
}

@Composable
private fun DisplaySettings(value: ProjectSettings, controller: SettingsController) {
    SettingsSection("Viewport") { ViewportControls(value, controller) }
    SettingsSection("Device orientation") {
        val orientationGroups = listOf(
            listOf(0 to "Landscape", 2 to "Reversed", 4 to "Both landscapes"),
            listOf(1 to "Portrait", 3 to "Upside down", 5 to "Both portraits"),
            listOf(6 to "All orientations"),
        )
        for (row in orientationGroups) {
            SettingsActions {
                for ((mode, label) in row) {
                    Tab(label, value.orientation == mode, { controller.edit { it.copy(orientation = mode) } })
                }
            }
        }
    }
    SettingsSection("Scaling") {
        Label("Stretch mode", SwingModifier.emptyBorder(JBUI.insetsTop(16)))
        ComboBox(ProjectSettings.stretches, value.stretch, onSelectionChange = { selected ->
            controller.edit { it.copy(stretch = selected ?: "disabled") }
        }) { Label(it.replace('_', ' ').replaceFirstChar(Char::uppercase)) }
        Label("Stretch aspect", SwingModifier.emptyBorder(JBUI.insetsTop(12)))
        ComboBox(ProjectSettings.aspects, value.aspect, onSelectionChange = { choice ->
            if (choice != null) controller.edit { it.copy(aspect = choice) }
        }) { Label(it.replace('_', ' ').replaceFirstChar(Char::uppercase)) }
    }
}
