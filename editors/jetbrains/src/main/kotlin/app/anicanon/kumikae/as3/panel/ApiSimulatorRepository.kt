package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Geometry
import app.anicanon.kumikae.as3.simulator.Pixel
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import java.io.ByteArrayInputStream
import java.util.Base64
import javax.imageio.ImageIO

/** The JSON transport under the repository: protocol version 1 methods and their results. */
internal interface SimulatorService : AutoCloseable {
    fun start(): String

    fun call(method: String, params: Map<String, Any> = emptyMap()): JsonElement

    /** Not a protocol method: only the owner, in the panel, switches private input on. */
    fun enableInput(): Boolean
}

/** The only place that maps protocol JSON to domain values. */
internal class ApiSimulatorRepository(private val service: SimulatorService) : SimulatorRepository {
    override fun connect(): String = service.start()

    override fun enableInput(): Boolean = service.enableInput()

    override fun devices(): List<SimulatorDevice> = service.call("devices").asJsonArray.map { device(it.asJsonObject) }

    override fun status(): SimulatorStatus = status(service.call("status"))

    override fun select(device: String): SimulatorStatus = status(service.call("select", mapOf("device" to device)))

    override fun boot(device: String): SimulatorStatus = status(service.call("boot", mapOf("device" to device)))

    override fun shutdown(device: String): SimulatorStatus = status(service.call("shutdown", mapOf("device" to device)))

    override fun install(device: String, appPath: String) {
        service.call("install", mapOf("device" to device, "path" to appPath))
    }

    override fun launch(device: String, bundle: String) {
        service.call("launch", mapOf("device" to device, "bundle" to bundle))
    }

    override fun screenshot(device: String): SimulatorFrame {
        val frame = service.call("screenshot", mapOf("device" to device, "format" to "jpeg")).asJsonObject
        val bytes = Base64.getDecoder().decode(frame["data"].asString)
        val image = ImageIO.read(ByteArrayInputStream(bytes)) ?: error("The Simulator returned an invalid picture.")
        return SimulatorFrame(frame["device"].asString, frame["generation"].asLong, image, frame["captureMs"].asLong)
    }

    override fun terminate(device: String, bundle: String) {
        service.call("terminate", mapOf("device" to device, "bundle" to bundle))
    }

    override fun consolePage(cursor: Long): ConsolePage {
        val reply = service.call("log", mapOf("cursor" to cursor)).asJsonObject
        val page = reply.getAsJsonObject("page")
        val launch = reply["launch"]?.takeIf { it.isJsonObject }?.asJsonObject
        val entries = page.getAsJsonArray("entries").map {
            val entry = it.asJsonObject
            ConsoleEntry(entry["stream"].asString, entry["text"].asString)
        }
        return ConsolePage(
            entries,
            page["nextCursor"].asLong,
            page["truncated"].asBoolean,
            // A reply that leaves the launch out says nothing about it: that is not "ended".
            running = launch?.get("running")?.takeUnless { it.isJsonNull }?.asBoolean ?: true,
            exitCode = launch?.get("exitCode")?.takeUnless { it.isJsonNull }?.asInt,
        )
    }

    override fun input(device: String, input: SimulatorInput) {
        val named = mapOf<String, Any>("device" to device)
        when (input) {
            is SimulatorInput.Tap -> service.call("tap", named + gesture(input.generation, input.geometry, input.at))

            is SimulatorInput.Swipe -> service.call(
                "swipe",
                named + gesture(input.generation, input.geometry, input.from) + mapOf(
                    "endX" to input.to.x,
                    "endY" to input.to.y,
                    "durationMs" to input.durationMs.coerceIn(MIN_SWIPE_MS, MAX_SWIPE_MS),
                ),
            )

            is SimulatorInput.Text -> service.call("text", named + ("text" to input.text))

            is SimulatorInput.Press -> service.call("button", named + ("button" to input.button.name.lowercase()))

            is SimulatorInput.Rotate ->
                service.call("rotate", named + ("orientation" to input.orientation.name.lowercase()))
        }
    }

    override fun close() = service.close()

    private fun gesture(generation: Long, geometry: Geometry, at: Pixel) = mapOf<String, Any>(
        "generation" to generation,
        "width" to geometry.width,
        "height" to geometry.height,
        "x" to at.x,
        "y" to at.y,
    )

    private fun status(result: JsonElement): SimulatorStatus {
        val state = result.asJsonObject
        val selected = state["device"]?.takeUnless { it.isJsonNull }?.asJsonObject
        return SimulatorStatus(selected?.let(::device), state["inputEnabled"].asBoolean, state["inputNotice"].asString)
    }

    private fun device(value: JsonObject) = SimulatorDevice(
        value["id"].asString,
        value["name"].asString,
        value["runtime"].asString,
        value["state"].asString,
    )

    private companion object {
        const val MIN_SWIPE_MS = 50L
        const val MAX_SWIPE_MS = 5000L
    }
}
