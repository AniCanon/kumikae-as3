package app.anicanon.kumikae.as3.language

import com.intellij.ide.plugins.PluginManager
import org.jetbrains.plugins.textmate.api.TextMateBundleProvider
import java.nio.file.Files

/**
 * Colours `.as` files with the same grammar the VS Code client uses. The build copies it from
 * `editors/vscode` into the plugin's folder, so there is one grammar, not two.
 */
class As3Grammar : TextMateBundleProvider {
    override fun getBundles(): List<TextMateBundleProvider.PluginBundle> {
        val folder = PluginManager.getPluginByClass(As3Grammar::class.java)?.pluginPath?.resolve(BUNDLE)
        return if (folder != null && Files.isDirectory(folder)) {
            listOf(TextMateBundleProvider.PluginBundle("ActionScript 3", folder))
        } else {
            emptyList()
        }
    }

    private companion object {
        const val BUNDLE = "textmate/actionscript"
    }
}
