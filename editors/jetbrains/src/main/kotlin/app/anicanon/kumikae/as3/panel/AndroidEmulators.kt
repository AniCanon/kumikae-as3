package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.run.AndroidDevices
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.util.ExecUtil
import java.nio.file.Files
import java.nio.file.Path

internal data class AndroidAvd(val name: String, val serial: String? = null)

internal enum class AndroidGraphics(val label: String, val arguments: List<String>) {
    DEVICE("Device default", emptyList()),
    HOST("Host GPU", listOf("-gpu", "host")),
    SOFTWARE("Software", listOf("-gpu", "software")),
}

internal class AndroidEmulators(
    private val devices: AndroidDevices,
    private val settings: com.intellij.ide.util.PropertiesComponent,
) {
    var graphics: AndroidGraphics
        get() = AndroidGraphics.entries.firstOrNull { it.name == settings.getValue(GRAPHICS) } ?: AndroidGraphics.DEVICE
        set(mode) {
            settings.setValue(GRAPHICS, mode.name, AndroidGraphics.DEVICE.name)
        }

    private companion object {
        const val GRAPHICS = "kumikae.as3.android.emulatorGraphics"
    }

    private val sdk: Path get() = requireNotNull(devices.sdk) {
        "Install Android SDK Platform Tools and Android Emulator."
    }
    private val adb get() = sdk.resolve("platform-tools/adb")
    private val emulator get() = sdk.resolve("emulator/emulator")
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
            Regex("^(emulator-[0-9]+)\\s+device$").matchEntire(it.trim())?.groupValues?.get(1)
        }.map { serial ->
            AndroidAvd(command(adb.toString(), "-s", serial, "emu", "avd", "name").lineSequence().first(), serial)
        }.toList()
        return (
            connected + installed.filter { name ->
                connected.none { it.name == name }
            }.map(::AndroidAvd)
            ).sortedBy { it.name }
    }
    fun start(name: String) {
        val arguments = mutableListOf(emulator.toString(), "-avd", name)
        if (PluginFiles.native()?.resolve("simulator-bridge")?.let(Files::isExecutable) ==
            true
        ) {
            arguments += "-no-window"
        }
        arguments += graphics.arguments
        ProcessBuilder(arguments).redirectErrorStream(true)
            .redirectOutput(ProcessBuilder.Redirect.DISCARD).start()
    }
    fun shutdown(serial: String) {
        command(adb.toString(), "-s", serial, "emu", "kill")
    }
    fun display(serial: String): AndroidGuestDisplay? {
        val output = command(adb.toString(), "-s", serial, "shell", "dumpsys", "input")
        val line = output.lineSequence().firstOrNull { it.contains("Viewport INTERNAL: displayId=0,") } ?: return null
        val rotation = Regex("""orientation=(\d+)""").find(line)?.groupValues?.get(1)?.toInt() ?: return null
        val size = Regex("""logicalFrame=\[0, 0, (\d+), (\d+)]""").find(line) ?: return null
        return AndroidGuestDisplay(rotation, size.groupValues[1].toInt(), size.groupValues[2].toInt())
    }
    fun open(serial: String, edge: Int): AndroidDisplayFeed {
        val helper = PluginFiles.native()?.resolve("simulator-bridge")
        check(helper != null && Files.isExecutable(helper)) {
            "Embedded Android display requires the macOS plugin build."
        }
        require(serial.matches(Regex("emulator-[0-9]+")))
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
