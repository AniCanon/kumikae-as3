package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.extensions.CustomLoadingExtensionPointBean
import com.intellij.openapi.extensions.ExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.util.xmlb.annotations.Attribute
import java.util.function.Function
import javax.swing.Action

/** Providers discover connected devices off the UI thread. Each action selects its device on the UI thread. */
class DeviceCatalogContribution : CustomLoadingExtensionPointBean<Function<Project, List<Action>>>() {
    @Attribute("label")
    var label = ""

    @Attribute("implementation")
    var implementation = ""
    override fun getImplementationClassName() = implementation

    internal fun read(project: Project): DeviceGroup = try {
        DeviceGroup(label, instance.apply(project))
    } catch (error: com.intellij.openapi.progress.ProcessCanceledException) {
        throw error
    } catch (error: Exception) {
        DeviceGroup(label, emptyList(), error.message ?: "Device discovery failed.")
    }
    companion object {
        val EP = ExtensionPointName.create<DeviceCatalogContribution>("app.anicanon.kumikae.as3.deviceGroup")
    }
}

internal data class DeviceGroup(val label: String, val devices: List<Action>, val emptyStatus: String = "Disconnected")
