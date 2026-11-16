package app.anicanon.kumikae.as3.scene

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.TitleButton
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.FlowLayout

@Composable
internal fun SceneView(controller: SceneController, choose: () -> Unit, modifier: SwingModifier = SwingModifier) {
    Panel(PanelLayout.Border(24, 16), modifier = modifier.emptyBorder(JBUI.insets(24))) {
        Column(SwingModifier.north()) {
            Label("Scene & AS3")
            StatusLine("The scene creates each native node. Its attached script supplies AS3 behavior.")
        }
        ScrollPane(modifier = SwingModifier.center()) {
            Panel(PanelLayout.Border(), modifier = SwingModifier.viewport()) {
                Column(SwingModifier.north()) {
                    for (node in controller.nodes) {
                        TitleButton(
                            if (node ==
                                controller.selected
                            ) {
                                "› ${node.path} · ${node.name}"
                            } else {
                                "${node.path} · ${node.name}"
                            },
                            "${node.type.ifEmpty { "Inherited node" }} · ${node.script ?: "No script"}",
                            { controller.selected = node },
                            chevron = false,
                        )
                    }
                }
            }
        }
        Column(SwingModifier.south()) {
            val node = controller.selected
            Label(node?.let { "${it.name} · ${it.type}" } ?: "No node selected")
            StatusLine(node?.script ?: "Attach an AS3 script to add lifecycle methods, signals and behavior.")
            if (node?.editable == false) StatusLine("Edit inherited or instanced scene attachments in Godot.")
            Panel(PanelLayout.Flow(FlowLayout.LEFT, 8, 8)) {
                Button("Open Script", { controller.open() })
                if (node?.editable == true) {
                    Button("Attach Script…", choose)
                    Button("Detach Script", { controller.attach(null) })
                }
                Button("Reload", { controller.reload() })
            }
            StatusLine(controller.status)
        }
    }
}
