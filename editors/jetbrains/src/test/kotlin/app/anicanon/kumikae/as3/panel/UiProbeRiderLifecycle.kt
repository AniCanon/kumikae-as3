package app.anicanon.kumikae.as3.panel

import com.intellij.ide.plugins.DynamicPluginVetoer
import com.intellij.ide.plugins.DynamicPlugins
import com.intellij.ide.plugins.PluginMainDescriptor
import com.intellij.ide.plugins.PluginManagerCore
import com.intellij.openapi.extensions.ExtensionPointName
import com.intellij.openapi.extensions.PluginId
import java.nio.file.Files
import java.nio.file.Path

internal object UiProbeRiderLifecycle {
    fun checkNativeUnload(destination: Path) {
        // This native point also contains legacy StartupActivity implementations; use its safe iterator.
        val activities = ExtensionPointName.create<Any>("com.intellij.postStartupActivity")
        check(activities.findFirstSafe { it.javaClass.name.endsWith(".RiderScopedActivity") } != null) {
            "Project-scoped database startup was not installed"
        }
        check(activities.findFirstSafe { it.javaClass.name.endsWith(".DatabaseConnectionDialogPromoter") } == null) {
            "Application-scoped database startup remains installed"
        }
        val vetoer = ExtensionPointName.create<DynamicPluginVetoer>("com.intellij.ide.dynamicPluginVetoer")
            .extensionList.single { it.javaClass.name.endsWith(".RiderStartupUnloadVetoer") }
        val observations = listOf("com.jetbrains.performancePlugin", "rider.intellij.plugin.appender").map { id ->
            val native = checkNotNull(PluginManagerCore.getPlugin(PluginId.getId(id)))
            check(vetoer.vetoPluginUnload(native) != null) {
                "Active startup integration must require restart to unload $id"
            }
            val reason = DynamicPlugins.validateCanUnloadWithoutRestart(native as PluginMainDescriptor)
            check(reason != null) { "Native plugin $id permits dynamic unload" }
            "$id: $reason"
        }
        Files.write(destination.resolve("native-startup-unload.txt"), observations)
    }
}
