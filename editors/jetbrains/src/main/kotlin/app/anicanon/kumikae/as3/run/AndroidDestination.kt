package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.panel.BackgroundWorker
import app.anicanon.kumikae.as3.testing.As3TestsConfiguration
import com.intellij.execution.DefaultExecutionTarget
import com.intellij.execution.ExecutionTarget
import com.intellij.execution.ExecutionTargetManager
import com.intellij.execution.ExecutionTargetProvider
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.execution.util.ExecUtil
import com.intellij.icons.AllIcons
import com.intellij.openapi.Disposable
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.service
import com.intellij.openapi.diagnostic.Logger
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit

internal data class AndroidDestination(val serial: String, val model: String) : ExecutionTarget() {
    override fun getId() = "kumikae.android.$serial"
    override fun getDisplayName() = "$model · $serial"
    override fun getIcon() = AllIcons.Nodes.Console
    override fun canRun(configuration: RunConfiguration) = configuration is AndroidRunConfiguration
}

internal fun androidDestination(line: String, supportedAbis: (String) -> String): AndroidDestination? {
    val fields = line.trim().split(WHITESPACE)
    if (fields.getOrNull(1) != "device") return null
    val serial = fields.first()
    if ("arm64-v8a" !in supportedAbis(serial).trim().split(',')) return null
    val model = fields.find { it.startsWith("model:") }?.substringAfter(':')?.replace('_', ' ') ?: "Android"
    return AndroidDestination(serial, model)
}

private val WHITESPACE = Regex("\\s+")

class AndroidDestinations : ExecutionTargetProvider() {
    override fun getTargets(project: Project, configuration: RunConfiguration): List<ExecutionTarget> =
        when (configuration) {
            is AndroidRunConfiguration -> project.service<AndroidDevices>().targets()

            is As3TestsConfiguration, is GodotRunConfiguration, is WebRunConfiguration ->
                listOf(DefaultExecutionTarget.INSTANCE)

            else -> emptyList()
        }
}

@Service(Service.Level.PROJECT)
internal class AndroidDevices(private val project: Project) : Disposable {
    private val adbName = if (SystemInfo.isWindows) "platform-tools/adb.exe" else "platform-tools/adb"

    val sdk: Path? get() {
        val home = Path.of(System.getProperty("user.home"))
        return listOfNotNull(
            System.getenv("ANDROID_HOME")?.takeIf(String::isNotBlank)?.let(Path::of),
            System.getenv("ANDROID_SDK_ROOT")?.takeIf(String::isNotBlank)?.let(Path::of),
            home.resolve("Library/Android/sdk"),
            home.resolve("Android/Sdk"),
        ).firstOrNull { Files.isExecutable(it.resolve(adbName)) }
    }
    private val worker = BackgroundWorker("Kumikae Android destinations")

    @Volatile private var devices = emptyList<AndroidDestination>()
    private var refreshAt = 0L

    @Synchronized fun targets(): List<AndroidDestination> {
        val now = System.nanoTime()
        if (refreshAt == 0L || now - refreshAt >= 0) {
            val submitted = worker.submit({
                val adb = sdk?.resolve(adbName) ?: return@submit emptyList()
                val result = ExecUtil.execAndGetOutput(GeneralCommandLine(adb.toString(), "devices", "-l"), 5000)
                check(result.exitCode == 0 && !result.isTimeout) {
                    result.stderr.ifBlank { "Could not list Android devices." }
                }
                result.stdoutLines.mapNotNull { line ->
                    androidDestination(line) { serial ->
                        val command = GeneralCommandLine(
                            adb.toString(),
                            "-s",
                            serial,
                            "shell",
                            "getprop",
                            "ro.product.cpu.abilist",
                        )
                        val query = ExecUtil.execAndGetOutput(command, 3000)
                        if (query.isTimeout || query.exitCode != 0) "" else query.stdout
                    }
                }
            }) { result ->
                if (project.isDisposed) return@submit
                val next = result.getOrElse {
                    Logger.getInstance(AndroidDevices::class.java).warn("Android device discovery failed", it)
                    emptyList()
                }
                if (next != devices) {
                    devices = next
                    ExecutionTargetManager.getInstance(project).update()
                }
            }
            if (submitted) refreshAt = now + TimeUnit.SECONDS.toNanos(3)
        }
        return devices
    }

    override fun dispose() = worker.dispose()
}
