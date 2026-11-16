package app.anicanon.kumikae.as3.language

import com.intellij.ide.plugins.DynamicPluginVetoer
import com.intellij.ide.plugins.IdeaPluginDescriptor
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.serviceIfCreated

// Inherit the native load policy without emitting a bridge to its internal default method.
@kotlin.jvm.JvmDefaultWithoutCompatibility
internal class RiderStartupUnloadVetoer : DynamicPluginVetoer {
    override fun vetoPluginUnload(pluginDescriptor: IdeaPluginDescriptor): String? {
        if (!needsRiderLifetimeRepair()) return null
        val app = ApplicationManager.getApplication()
        val installed = when (pluginDescriptor.pluginId.idString) {
            "com.jetbrains.performancePlugin" -> app.serviceIfCreated<RiderFeedbackLifetime>()?.installed
            "rider.intellij.plugin.appender" -> app.serviceIfCreated<RiderDatabaseLifetime>()?.installed
            else -> false
        }
        return if (installed == true) {
            "Restart Rider to unload project startup services while Kumikae AS3 is active"
        } else {
            null
        }
    }
}
