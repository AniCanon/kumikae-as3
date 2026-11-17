package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
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
import java.awt.FlowLayout

@Composable
internal fun ProjectSettingsView(
    controller: SettingsController,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
    modifier: SwingModifier = SwingModifier,
    initialPage: String = "General",
    addTarget: (() -> Unit)? = null,
) {
    var page by remember { mutableStateOf(initialPage) }
    Panel(PanelLayout.Border(24, 16), modifier = modifier.emptyBorder(JBUI.insets(24))) {
        Panel(PanelLayout.Border(), modifier = SwingModifier.west()) {
            Column(SwingModifier.north()) {
                for (name in listOf("General", "Display", "Export")) {
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

                            "Export" -> {
                                if (addTarget !=
                                    null
                                ) {
                                    Button("Add Game Target…", addTarget, SwingModifier.enabled(!controller.dirty))
                                }
                                ExportSettings(draft, controller, choose)
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
    ComboBox(
        ProjectSettings.orientations,
        ProjectSettings.orientations.getOrNull(value.orientation),
        onSelectionChange = { selected ->
            controller.edit { it.copy(orientation = ProjectSettings.orientations.indexOf(selected)) }
        },
    ) { Label(it) }
    StatusLine("Applies to mobile exports. Rebuild the app after changing it.")

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
    Label("Export preset")
    val preset = value.presets.firstOrNull { it.section == value.preset }
    ComboBox(value.presets, preset, onSelectionChange = { selected ->
        controller.edit { it.select(selected?.section.orEmpty()) }
    }) { Label(it.toString()) }
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
