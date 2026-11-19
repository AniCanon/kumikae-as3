package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.run.AndroidDevices
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.util.ExecUtil
import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.util.SystemInfo
import java.nio.file.Files
import java.nio.file.Path

internal data class AndroidAvd(val name: String, val serial: String? = null)

internal enum class AndroidGraphics(val label: String, val arguments: List<String>) {
    DEVICE("Device default", emptyList()),
    HOST("Host GPU", listOf("-gpu", "host")),
    SOFTWARE("Software", listOf("-gpu", "software")),
}

internal class AndroidEmulators(private val devices: AndroidDevices, private val settings: PropertiesComponent) {
    var graphics: AndroidGraphics
        get() = AndroidGraphics.entries.firstOrNull { it.name == settings.getValue(GRAPHICS) } ?: AndroidGraphics.DEVICE
        set(mode) {
            settings.setValue(GRAPHICS, mode.name, AndroidGraphics.DEVICE.name)
        }

    private companion object {
        const val GRAPHICS = "kumikae.as3.android.emulatorGraphics"
        val DEVICE_ROW = Regex("^(emulator-[0-9]+)\\s+device$")
        val SERIAL_PATTERN = Regex("emulator-[0-9]+")
        val ORIENTATION = Regex("""orientation=(\d+)""")
        val LOGICAL_FRAME = Regex("""logicalFrame=\[0, 0, (\d+), (\d+)]""")
    }

    private val sdk: Path get() = requireNotNull(devices.sdk) {
        "Install Android SDK Platform Tools and Android Emulator."
    }
    private val suffix get() = if (SystemInfo.isWindows) ".exe" else ""
    private val adb get() = sdk.resolve("platform-tools/adb$suffix")
    private val emulator get() = sdk.resolve("emulator/emulator$suffix")
    private fun command(vararg arguments: String): String {
        val result = ExecUtil.execAndGetOutput(GeneralCommandLine(*arguments), 7000)
        check(result.exitCode == 0 && !result.isTimeout && !result.stdout.startsWith("KO:")) {
            result.stderr.ifBlank { result.stdout.ifBlank { "Android SDK command timed out." } }
        }
        return result.stdout.trim()
    }
    fun list(): List<AndroidAvd> {
        val installed = command(emulator.toString(), "-list-avds").lines().filter(String::isNotBlank)
        val connected = command(adb.toString(), "devices").lineSequence().mapNotNull {
            DEVICE_ROW.matchEntire(it.trim())?.groupValues?.get(1)
        }.map { serial ->
            val name = runCatching {
                command(adb.toString(), "-s", serial, "emu", "avd", "name").lineSequence().firstOrNull()
            }.getOrNull()?.takeIf(String::isNotBlank) ?: serial
            AndroidAvd(name, serial)
        }.toList()
        return (
            connected + installed.filter { name ->
                connected.none { it.name == name }
            }.map(::AndroidAvd)
            ).sortedBy { it.name }
    }
    fun start(name: String) {
        val arguments = mutableListOf(emulator.toString(), "-avd", name)
        if (PluginFiles.native()?.resolve("simulator-bridge")?.let(Files::isExecutable) == true) {
            arguments += "-no-window"
        }
        arguments += graphics.arguments
        ProcessBuilder(arguments).redirectErrorStream(true)
            .redirectOutput(ProcessBuilder.Redirect.DISCARD).start()
    }
    fun shutdown(serial: String) {
        command(adb.toString(), "-s", serial, "emu", "kill")
        if (SERIAL_PATTERN.matches(serial)) {
            AndroidCaptureLease.forget("videmulator" + serial.substringAfter('-'))
        }
    }
    fun display(serial: String): AndroidGuestDisplay? {
        val output = command(adb.toString(), "-s", serial, "shell", "dumpsys", "input")
        val line = output.lineSequence().firstOrNull { it.contains("Viewport INTERNAL: displayId=0,") } ?: return null
        val rotation = ORIENTATION.find(line)?.groupValues?.get(1)?.toInt() ?: return null
        val size = LOGICAL_FRAME.find(line) ?: return null
        return AndroidGuestDisplay(rotation, size.groupValues[1].toInt(), size.groupValues[2].toInt())
    }
    fun open(serial: String, edge: Int): AndroidDisplayFeed {
        val helper = PluginFiles.native()?.resolve("simulator-bridge")
        check(helper != null && Files.isExecutable(helper)) {
            "Embedded Android display requires the macOS plugin build."
        }
        require(SERIAL_PATTERN.matches(serial)) { "Unsupported Android emulator serial: $serial" }
        val handle = "videmulator" + serial.substringAfter('-')
        val lease = AndroidCaptureLease.acquire(
            handle,
            { command(helper.toString(), "--android-display-active", handle) == "active" },
            {
                val actual = command(adb.toString(), "-s", serial, "emu", "screenrecord", "webrtc", "start", "60")
                check(actual.lineSequence().first() == handle) { "The emulator did not provide its shared display." }
            },
            { command(adb.toString(), "-s", serial, "emu", "screenrecord", "webrtc", "stop") },
        )
        return try {
            AndroidDisplayFeed(helper, handle, adb, serial, edge, lease)
        } catch (error: Exception) {
            lease.close()
            throw error
        }
    }
}
