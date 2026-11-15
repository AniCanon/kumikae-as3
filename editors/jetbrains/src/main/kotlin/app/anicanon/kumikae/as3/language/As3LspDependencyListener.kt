package app.anicanon.kumikae.as3.language

import com.intellij.ide.plugins.PluginManagerCore
import com.intellij.openapi.application.ApplicationInfo
import com.intellij.openapi.extensions.PluginId
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.fileEditor.FileEditorManagerListener
import com.intellij.openapi.vfs.VirtualFile

/** Optional dependencies must not leave IDEA users with silently missing semantic features. */
class As3LspDependencyListener : FileEditorManagerListener {
    override fun fileOpened(source: FileEditorManager, file: VirtualFile) {
        if (file.extension != "as" || ApplicationInfo.getInstance().build.productCode !in setOf("IU", "IC")) return
        val dependency = PluginId.getId("com.redhat.devtools.lsp4ij")
        if (!PluginManagerCore.isPluginInstalled(dependency) || PluginManagerCore.isDisabled(dependency)) {
            source.project.getService(As3ProjectLanguageService::class.java).warnMissingClient()
        }
    }
}
