package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.SurfaceGroup
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder

@Composable
internal fun GodotProperties(source: String) {
    val config = remember(source) { GodotConfig(source) }
    var expanded by remember { mutableStateOf(setOf("application")) }
    for (section in config.sections) {
        val keys = config.properties(section)
        if (keys.isEmpty()) continue
        val open = section in expanded
        Button((if (open) "▾  " else "▸  ") + section.ifBlank { "Project file" }, {
            expanded = if (open) expanded - section else expanded + section
        })
        if (open) {
            SurfaceGroup(SwingModifier.emptyBorder(JBUI.insetsBottom(8)), padding = 10) {
                for (key in keys) {
                    Label(key)
                    StatusLine(config.raw(section, key).orEmpty())
                }
            }
        }
    }
}
