package app.anicanon.kumikae.as3.scene

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.FlowLayout

@Composable
internal fun SceneView(controller: SceneController, choose: () -> Unit, modifier: SwingModifier = SwingModifier) {
    Panel(PanelLayout.Border(24, 16), modifier = modifier.emptyBorder(JBUI.insets(24))) {
        SceneTree(
            controller.nodes,
            controller.selected,
            { controller.selected = it },
            SwingModifier.center(),
        )
        Column(SwingModifier.south()) {
            val node = controller.selected
            Label(
                node?.let {
                    if (it.type.isEmpty()) it.name else "${it.name} · ${it.type}"
                } ?: "No node selected",
            )
            node?.properties?.entries?.take(8)?.forEach { (name, value) -> StatusLine("$name   $value") }
            node?.script?.let { StatusLine(it) }
            if (node?.editable == false) StatusLine("Edit inherited or instanced scene attachments in Godot.")
            Panel(PanelLayout.Flow(FlowLayout.LEFT, 8, 8)) {
                if (node?.script != null) Button("Open Script", { controller.open() })
                if (node?.editable == true) {
                    Button("Attach Script…", choose)
                    if (node.script != null) Button("Detach Script", { controller.attach(null) })
                }
                Button("Open in Godot", { controller.openInGodot() })
                Button("Reload", { controller.reload() })
            }
            StatusLine(controller.status)
        }
    }
}
