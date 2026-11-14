package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Geometry
import app.anicanon.kumikae.as3.simulator.Pixel
import java.awt.image.BufferedImage

internal data class SimulatorDevice(val id: String, val name: String, val runtime: String, val state: String) {
    val label: String get() = "$name · $runtime · $state"
}

internal data class SimulatorStatus(val device: SimulatorDevice?, val inputEnabled: Boolean, val notice: String)

/** One decoded picture. `generation` is the service's geometry counter that input must quote back. */
internal class SimulatorFrame(val device: String, val generation: Long, val image: BufferedImage, val captureMs: Long) {
    val geometry = Geometry(image.width, image.height)
}

internal data class ConsoleEntry(val stream: String, val text: String)

/** `running` and `exitCode` are the launched app's: a page read after it ended says so. */
internal data class ConsolePage(
    val entries: List<ConsoleEntry>,
    val nextCursor: Long,
    val truncated: Boolean,
    val running: Boolean = true,
    val exitCode: Int? = null,
)

internal enum class HardwareButton { HOME, LOCK }

internal enum class Orientation { PORTRAIT, LANDSCAPE_LEFT, LANDSCAPE_RIGHT, PORTRAIT_UPSIDE_DOWN }

internal sealed interface SimulatorInput {
    data class Tap(val generation: Long, val geometry: Geometry, val at: Pixel) : SimulatorInput

    data class Swipe(
        val generation: Long,
        val geometry: Geometry,
        val from: Pixel,
        val to: Pixel,
        val durationMs: Long,
    ) : SimulatorInput

    data class Text(val text: String) : SimulatorInput

    data class Press(val button: HardwareButton) : SimulatorInput

    data class Rotate(val orientation: Orientation) : SimulatorInput
}
