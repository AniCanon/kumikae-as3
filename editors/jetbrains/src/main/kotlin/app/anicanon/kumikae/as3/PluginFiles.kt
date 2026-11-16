package app.anicanon.kumikae.as3

import com.intellij.openapi.application.PathManager
import com.intellij.openapi.util.IconLoader
import java.nio.file.Files
import java.nio.file.Path
import javax.swing.Icon

/** What the plugin carries beside its code: the compiler and the Godot extension, and its mark. */
internal object PluginFiles {
    /** The mark shown for everything of this plugin's in menus and lists. */
    val mark: Icon = IconLoader.getIcon("/META-INF/brandMark.svg", PluginFiles::class.java)

    /** An AS3 script's icon. */
    val script: Icon = IconLoader.getIcon("/icons/as3File.svg", PluginFiles::class.java)

    /**
     * The folder with `kas3c` and the extension's library, when the plugin was built with them. The
     * plugin's folder holds lib/<this jar>; `native` is beside lib.
     */
    fun native(): Path? = PathManager.getJarPathForClass(PluginFiles::class.java)?.let(Path::of)
        ?.parent?.parent?.resolve("native")?.takeIf { Files.isDirectory(it) }?.also(NativePrograms::makeRunnable)
}
