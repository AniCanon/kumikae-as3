package app.anicanon.kumikae.as3.scene

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.IconButton
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import com.intellij.icons.AllIcons
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import java.awt.FlowLayout

@Composable
internal fun SceneView(controller: SceneController, choose: () -> Unit, modifier: SwingModifier = SwingModifier) {
    Panel(PanelLayout.Border(0, 10), modifier = modifier.emptyBorder(JBUI.insets(16))) {
        Panel(PanelLayout.Border(8, 0), modifier = SwingModifier.north()) {
            Label("Scene hierarchy", SwingModifier.center().font(JBFont.h2().asBold()))
            Panel(PanelLayout.Flow(FlowLayout.RIGHT, 4, 0), modifier = SwingModifier.east()) {
                Button("Open in Godot", controller::openInGodot)
                IconButton(AllIcons.Actions.Refresh, "Reload scene", { controller.reload() })
            }
        }
        SceneTree(
            controller.nodes,
            controller.selected,
            { controller.selected = it },
            SwingModifier.center(),
        )
        Column(SwingModifier.south()) {
            val node = controller.selected
            val title = node?.let { if (it.type.isBlank()) it.name else "${it.name} · ${it.type}" } ?: "Node"
            SettingsSection(title) {
                if (node == null) {
                    StatusLine("Select a node to inspect its properties and script.")
                } else {
                    for ((name, value) in node.properties.entries.take(8)) StatusLine("$name   $value")
                    StatusLine(node.script ?: "No script attached")
                    if (!node.editable) StatusLine("Edit inherited or instanced scene attachments in Godot.")
                    Panel(PanelLayout.Flow(FlowLayout.LEFT, 4, 2)) {
                        if (node.script != null) Button("Open Script", controller::open)
                        if (node.editable) {
                            Button("Attach Script…", choose)
                            if (node.script != null) Button("Detach Script", { controller.attach(null) })
                        }
                    }
                }
            }
            if (controller.status.isNotBlank()) StatusLine(controller.status)
        }
    }
}
