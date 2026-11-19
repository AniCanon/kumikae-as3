package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.Tab
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import org.jetbrains.compose.swing.modifier.interaction.enabled
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.FlowLayout
import javax.swing.JLabel

@Composable
internal fun ProjectSettingsView(
    controller: SettingsController,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
    modifier: SwingModifier = SwingModifier,
    initialPage: String = "General",
    addTarget: (() -> Unit)? = null,
    export: ((ProjectSettings, List<ExportPreset>) -> Unit)? = null,
) {
    var page by remember { mutableStateOf(initialPage) }
    Panel(PanelLayout.Border(14, 10), modifier = modifier.emptyBorder(JBUI.insets(16))) {
        Panel(PanelLayout.Border(), modifier = SwingModifier.west()) {
            Column(SwingModifier.north()) {
                SwingNode(factory = {
                    JLabel("Kumikae AS3", PluginFiles.mark, JLabel.LEADING)
                        .apply { border = JBUI.Borders.emptyBottom(8) }
                })
                for (name in listOf("General", "Display", "Export", "Properties")) {
                    Tab(name, page == name, { page = name })
                }
            }
        }
        ScrollPane(modifier = SwingModifier.center()) {
            Panel(PanelLayout.Border(), modifier = SwingModifier.viewport()) {
                Column(SwingModifier.north().emptyBorder(JBUI.insets(0, 8, 16, 8))) {
                    Label("$page settings", SwingModifier.font(JBFont.h2()).emptyBorder(JBUI.insetsBottom(20)))
                    val draft = controller.draft
                    if (draft != null) {
                        when (page) {
                            "General" -> GeneralSettings(draft, controller, choose)

                            "Display" -> DisplaySettings(draft, controller)

                            "Properties" -> GodotProperties(draft.projectText)

                            "Export" -> {
                                if (addTarget !=
                                    null
                                ) {
                                    Button("Add Game Target…", addTarget, SwingModifier.enabled(!controller.dirty))
                                }
                                ExportSettings(draft, controller, choose)
                                if (export != null && draft.presets.isNotEmpty()) {
                                    Panel(PanelLayout.Flow(FlowLayout.LEFT)) {
                                        draft.presets.find { it.section == draft.preset }?.let { selected ->
                                            Button(
                                                "Export ${selected.name}…",
                                                { export(draft, listOf(selected)) },
                                                SwingModifier.enabled(!controller.dirty),
                                            )
                                        }
                                        if (draft.presets.size >
                                            1
                                        ) {
                                            Button(
                                                "Export All…",
                                                { export(draft, draft.presets) },
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
        Column(SwingModifier.south()) {
            StatusLine(controller.status)
            Panel(PanelLayout.Flow(FlowLayout.RIGHT, 8, 8)) {
                Button("Reload", { controller.reload() })
                Button("Apply", { controller.apply() }, SwingModifier.enabled(controller.dirty))
            }
        }
    }
}

@Composable
private fun GeneralSettings(
    value: ProjectSettings,
    controller: SettingsController,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
) {
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
    Panel(PanelLayout.Border(20, 0), modifier = SwingModifier.emptyBorder(JBUI.insetsTop(16))) {
        Column(SwingModifier.west()) {
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

@Composable
private fun DisplaySettings(value: ProjectSettings, controller: SettingsController) {
    ViewportControls(value, controller)
    Label("Orientation", SwingModifier.emptyBorder(JBUI.insetsTop(8)))
    val orientationGroups = listOf(
        listOf(0 to "Landscape", 2 to "Reversed", 4 to "Both landscapes"),
        listOf(1 to "Portrait", 3 to "Upside down", 5 to "Both portraits"),
        listOf(6 to "All orientations"),
    )
    for (row in orientationGroups) {
        Panel(PanelLayout.Flow(FlowLayout.LEFT, 4, 2)) {
            for ((mode, label) in row) {
                Tab(label, value.orientation == mode, { controller.edit { it.copy(orientation = mode) } })
            }
        }
    }

    Label("Stretch mode", SwingModifier.emptyBorder(JBUI.insetsTop(16)))
    ComboBox(ProjectSettings.stretches, value.stretch, onSelectionChange = { selected ->
        controller.edit { it.copy(stretch = selected ?: "disabled") }
    }) { Label(it.replace('_', ' ').replaceFirstChar(Char::uppercase)) }
    Label("Stretch aspect", SwingModifier.emptyBorder(JBUI.insetsTop(12)))
    ComboBox(ProjectSettings.aspects, value.aspect, onSelectionChange = { choice ->
        if (choice != null) controller.edit { it.copy(aspect = choice) }
    }) { Label(it.replace('_', ' ').replaceFirstChar(Char::uppercase)) }
}

@Composable
private fun ExportSettings(
    value: ProjectSettings,
    controller: SettingsController,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
) {
    if (value.presets.isEmpty()) {
        Label("No export presets")
        return
    }
    val preset = value.presets.firstOrNull { it.section == value.preset }
    for (target in value.presets) {
        Tab(target.toString(), target == preset, { controller.edit { it.select(target.section) } })
    }
    if (preset?.identifierKey != null) {
        LabelledField(
            if (preset.platform == "iOS") "Bundle identifier" else "Package name",
            value.identifier,
            { text -> controller.edit { it.copy(identifier = text) } },
            SwingModifier.emptyBorder(JBUI.insetsTop(16)),
        )
        ArtworkField(GameResourceKind.ExportIcon, value.exportIcon, controller.exportPreview, choose) { path ->
            controller.edit { it.copy(exportIcon = path) }
        }
    } else {
        StatusLine("This preset has no mobile application identifier.")
    }
    StatusLine("Signing credentials remain in the platform export configuration.")
}
