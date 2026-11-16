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
import java.awt.FlowLayout

@Composable
internal fun ProjectSettingsView(
    controller: SettingsController,
    choose: (String, (String) -> Unit) -> Unit,
    modifier: SwingModifier = SwingModifier,
) {
    var page by remember { mutableStateOf("General") }
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
                            "Export" -> ExportSettings(draft, controller, choose)
                        }
                    }
                }
            }
        }
        Column(SwingModifier.south()) {
            StatusLine(controller.status)
            Panel(PanelLayout.Flow(FlowLayout.RIGHT, 8, 8)) {
                Button("Reload", { controller.reload() })
                Button("Apply", { controller.apply() })
            }
        }
    }
}

@Composable
private fun GeneralSettings(
    value: ProjectSettings,
    controller: SettingsController,
    choose: (String, (String) -> Unit) -> Unit,
) {
    ProjectIconPreview(controller.preview)
    if (value.icon.isNotEmpty() && controller.preview == null) {
        StatusLine("Preview unavailable · choose a valid image in this project.")
    }
    LabelledField("Application name", value.name, { text -> controller.edit { it.copy(name = text) } })
    ResourceField("Main scene", value.mainScene, { text -> controller.edit { it.copy(mainScene = text) } }, choose)
    ResourceField("Application icon", value.icon, { text -> controller.edit { it.copy(icon = text) } }, choose)
    StatusLine("The scene creates the nodes. Attached AS3 scripts supply their behavior.")
}

@Composable
private fun DisplaySettings(value: ProjectSettings, controller: SettingsController) {
    LabelledField("Viewport width (px)", value.width, { text -> controller.edit { it.copy(width = text) } })
    LabelledField("Viewport height (px)", value.height, { text -> controller.edit { it.copy(height = text) } })
    Label("Orientation", SwingModifier.emptyBorder(JBUI.insetsTop(8)))
    ComboBox(
        ProjectSettings.orientations,
        ProjectSettings.orientations.getOrNull(value.orientation),
        onSelectionChange = { selected ->
            controller.edit { it.copy(orientation = ProjectSettings.orientations.indexOf(selected)) }
        },
    ) { Label(it) }
    StatusLine("Applies to mobile exports. Rebuild the app after changing it.")
    StatusLine("The game must provide a layout for each supported orientation.")
    Label("Stretch mode", SwingModifier.emptyBorder(JBUI.insetsTop(16)))
    ComboBox(ProjectSettings.stretches, value.stretch, onSelectionChange = { selected ->
        controller.edit { it.copy(stretch = selected ?: "disabled") }
    }) { Label(it) }
}

@Composable
private fun ExportSettings(
    value: ProjectSettings,
    controller: SettingsController,
    choose: (String, (String) -> Unit) -> Unit,
) {
    if (value.presets.isEmpty()) {
        Label("No export presets")
        StatusLine("Create a platform preset in Godot, then reload these settings.")
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
        ResourceField("Platform icon override", value.exportIcon, { text ->
            controller.edit { it.copy(exportIcon = text) }
        }, choose)
    } else {
        StatusLine("This preset has no mobile application identifier.")
    }
    StatusLine("Signing credentials remain in the platform export configuration.")
}

@Composable
private fun ResourceField(
    label: String,
    value: String,
    change: (String) -> Unit,
    choose: (String, (String) -> Unit) -> Unit,
) {
    LabelledField(label, value, change)
    Panel(PanelLayout.Flow(FlowLayout.LEFT, 0, 0), modifier = SwingModifier.emptyBorder(JBUI.insetsBottom(16))) {
        Button("Choose $label…", { choose(label, change) })
    }
}
