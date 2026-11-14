package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser

data class Device(val id: String, val name: String, val runtime: String, val state: String)
object Devices {
    fun parse(json: String): List<Device> = try {
        val root = JsonParser.parseString(json).asJsonObject
        val devices = root.getAsJsonObject("devices") ?: error("Missing devices")
        devices.entrySet().filter { it.key.startsWith("com.apple.CoreSimulator.SimRuntime.iOS-") }
            .flatMap { (runtime, entries) ->
                entries.asJsonArray.filter { it.asJsonObject["isAvailable"]?.asBoolean == true }.map {
                    val d = it.asJsonObject
                    Device(
                        d["udid"].asString,
                        d["name"].asString,
                        "iOS " + runtime.substringAfter(".iOS-").replace('-', '.'),
                        d["state"].asString,
                    )
                }
            }.sortedWith(compareBy(Device::runtime, Device::name, Device::id))
    } catch (e: RuntimeException) {
        throw IllegalArgumentException("Invalid simctl device list", e)
    }
}
