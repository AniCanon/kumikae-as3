package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

/** Installed SDK only, and every install/input belongs to the disposable device owner. */
internal class NativeProbeApp(private val runner: CommandRunner) {
    fun <T> use(family: String = "iPhone", work: (String, Path) -> T): T {
        val runtime = JsonParser.parseString(simctl("list", "runtimes", "--json")).asJsonObject
            .getAsJsonArray("runtimes").map { it.asJsonObject }
            .first { it["isAvailable"].asBoolean && it["name"].asString == "iOS 26.4" }
        val type = runtime.getAsJsonArray("supportedDeviceTypes").map { it.asJsonObject }
            .first { it["productFamily"].asString == family }["identifier"].asString
        val app = buildApp()
        return ProbeDeviceOwner(runner).use(type, runtime["identifier"].asString) { id ->
            simctl("boot", id)
            simctl("bootstatus", id, "-b")
            simctl("install", id, app.toString())
            simctl("launch", id, "org.kumikae.nativeprobe")
            val report = Path.of(simctl("get_app_container", id, "org.kumikae.nativeprobe", "data").trim())
                .resolve("Documents/probe.json")
            await("app report") { Files.exists(report) }
            work(id, report)
        }
    }
    private fun buildApp(): Path {
        val source = Path.of("../native/SimulatorBridge/ProbeApp").toAbsolutePath()
        val app = Path.of("build/native-probe/NativeProbe.app").toAbsolutePath()
        Files.createDirectories(app)
        Files.copy(source.resolve("Info.plist"), app.resolve("Info.plist"), StandardCopyOption.REPLACE_EXISTING)
        val sdk = runner.run(listOf("/usr/bin/xcrun", "--sdk", "iphonesimulator", "--show-sdk-path"), 15_000).trim()
        runner.run(
            listOf(
                "/usr/bin/xcrun", "swiftc", "-swift-version", "6", "-strict-concurrency=complete", "-parse-as-library", "-sdk", sdk,
                "-target", "arm64-apple-ios17.0-simulator", source.resolve("ProbeApp.swift").toString(),
                "-o", app.resolve("NativeProbe").toString(),
            ),
            120_000,
        )
        return app
    }
    private fun simctl(vararg args: String) = runner.run(listOf("/usr/bin/xcrun", "simctl") + args, 120_000)
    companion object {
        fun read(path: Path) = JsonParser.parseString(Files.readString(path)).asJsonObject
        fun await(description: String, condition: () -> Boolean) {
            val deadline = System.nanoTime() + 20_000_000_000L
            while (System.nanoTime() < deadline) {
                if (condition()) return
                Thread.sleep(20)
            }
            error("Timed out waiting for $description")
        }
    }
}
