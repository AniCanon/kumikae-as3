package app.anicanon.kumikae.as3.language

import com.intellij.openapi.application.PathManager
import org.jetbrains.plugins.textmate.api.TextMateBundleProvider
import java.nio.file.Files
import java.nio.file.Path

/**
 * Colours `.as` files with the same grammar the VS Code client uses. The build copies it from
 * `editors/vscode` into the plugin's folder, so there is one grammar, not two.
 */
class As3Grammar : TextMateBundleProvider {
    override fun getBundles(): List<TextMateBundleProvider.PluginBundle> {
        // The plugin's folder holds lib/<this jar>; the grammar is beside lib.
        val folder = PathManager.getJarPathForClass(
            As3Grammar::class.java,
        )?.let(Path::of)?.parent?.parent?.resolve(BUNDLE)
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
