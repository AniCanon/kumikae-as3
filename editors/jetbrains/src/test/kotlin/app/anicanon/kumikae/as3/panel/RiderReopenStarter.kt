package app.anicanon.kumikae.as3.panel

import com.intellij.ide.plugins.PluginManagerCore
import com.intellij.openapi.application.ApplicationInfo
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ApplicationStarter
import com.intellij.openapi.application.ex.ApplicationEx
import java.nio.file.Files
import java.nio.file.Path

/** Test-only starter loaded independently of the plugin under test. */
class RiderReopenStarter : ApplicationStarter {
    override val requiredModality = ApplicationStarter.NOT_IN_EDT
    override val isHeadless = false

    override fun main(args: List<String>) {
        val report = Files.createDirectories(Path.of(args[1]))
        val failure = runCatching {
            val expected = args[2] == "Enabled"
            val loaded = PluginManagerCore.loadedPlugins.any { it.pluginId.idString == "app.anicanon.kumikae.as3" }
            Files.writeString(
                report.resolve("environment.txt"),
                "${ApplicationInfo.getInstance().build} Kumikae loaded=$loaded\n",
            )
            check(loaded == expected) { "Expected Kumikae loaded=$expected, observed $loaded" }
            UiProbeFeedbackGuard.run(report) { UiProbeRiderReopen.run(report, observeNativePanels = true) }
        }.exceptionOrNull()
        Files.writeString(report.resolve("result.txt"), failure?.stackTraceToString() ?: "passed\n")
        val app = ApplicationManager.getApplication() as ApplicationEx
        app.invokeLater { app.exit(true, true, if (failure == null) 0 else 1) }
    }
}
