package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.simulator.CommandRunner
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import java.nio.file.Files

internal data class IosDevice(
    val id: String,
    val name: String,
    val available: Boolean,
    val detail: String,
    val buildId: String = id,
)

internal data class IosDeviceSnapshot(val devices: List<IosDevice>, val errors: List<String>)

internal object IosDevices {
    fun physical(json: String): List<IosDevice> = try {
        val result = JsonParser.parseString(json).asJsonObject.getAsJsonObject("result")
            ?: error("Missing result")
        val devices = result.getAsJsonArray("devices") ?: error("Missing devices")
        devices.map { it.asJsonObject }.mapNotNull { device ->
            val properties = device.getAsJsonObject("properties")
            val hardware = properties?.getAsJsonObject("hardware") ?: device.getAsJsonObject("hardwareProperties")
            val state = properties?.getAsJsonObject("state") ?: device.getAsJsonObject("deviceProperties")
            val connection = properties?.getAsJsonObject("connection") ?: device.getAsJsonObject("connectionProperties")
            if (hardware?.text("platform") != "iOS" || hardware.text("reality") == "simulated") return@mapNotNull null
            val status = connection?.text("state").orEmpty().ifEmpty { connection?.text("tunnelState").orEmpty() }
            IosDevice(
                device.text("identifier").ifEmpty { error("Device has no identifier") },
                state?.text("name").orEmpty().ifEmpty { "iOS device" },
                // A paired device can open a tunnel on demand. Xcode lists it as available.
                status == "connected" || (status == "disconnected" && connection?.text("pairingState") == "paired"),
                status.ifEmpty { "unavailable" },
                hardware.text("udid").ifEmpty { device.text("identifier") },
            )
        }
    } catch (failure: RuntimeException) {
        throw IllegalArgumentException("Invalid devicectl device list: ${failure.message}", failure)
    }

    private fun JsonObject.text(key: String): String = get(key)?.takeUnless { it.isJsonNull }?.asString.orEmpty()
}

internal class IosDeviceRepository(private val runner: CommandRunner) {
    fun discover(): IosDeviceSnapshot {
        val output = Files.createTempFile("kumikae-devices-", ".json")
        try {
            runner.run(
                listOf("/usr/bin/xcrun", "devicectl", "list", "devices", "--json-output", output.toString()),
                15_000L,
            )
            return IosDeviceSnapshot(IosDevices.physical(Files.readString(output)), emptyList())
        } finally {
            Files.deleteIfExists(output)
        }
    }
}
