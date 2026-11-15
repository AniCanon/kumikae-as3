package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.FrameFeed
import app.anicanon.kumikae.as3.simulator.LatestValue
import app.anicanon.kumikae.as3.simulator.LiveFrame
import com.google.gson.Gson
import java.awt.image.BufferedImage
import java.awt.image.DataBufferInt
import java.lang.ref.WeakReference

/** Fresh full-size images, through the production repository; no Simulator or host input. */
internal class UiProbeMemoryService : SimulatorService {
    private val gson = Gson()
    private val device =
        mapOf("id" to "synthetic", "name" to "Synthetic phone", "runtime" to "iOS", "state" to "Booted")
    private val slot = LatestValue<LiveFrame>()
    private val images = mutableListOf<WeakReference<BufferedImage>>()
    private var selected = false
    private var closed = false
    var produced = 0
        private set
    val retained: Int get() = images.count { it.get() != null }

    fun publish() {
        check(!closed)
        images.removeAll { it.get() == null }
        val image = BufferedImage(1206, 2622, BufferedImage.TYPE_INT_ARGB_PRE)
        (image.raster.dataBuffer as DataBufferInt).data.fill(0xff000000.toInt() or produced)
        produced++
        images += WeakReference(image)
        slot.put(LiveFrame("synthetic", 1, image, System.nanoTime(), System.nanoTime()))
    }

    override fun start() = "/synthetic/session.json"
    override fun enableInput() = true
    override fun call(method: String, params: Map<String, Any>) = gson.toJsonTree(
        when (method) {
            "devices" -> listOf(device)

            "select", "status" -> {
                if (method == "select") selected = true
                mapOf(
                    "device" to device.takeIf { selected },
                    "inputEnabled" to true,
                    "inputNotice" to "Synthetic frame retention probe",
                    "continuousTouch" to true,
                )
            }

            "log" -> mapOf("page" to mapOf("entries" to emptyList<Any>(), "nextCursor" to 0, "truncated" to false))

            else -> error("Memory probe refuses $method")
        },
    )

    override fun openFrames(device: String): FrameFeed {
        check(device == "synthetic")
        return object : FrameFeed {
            override val failure: Throwable? = null
            override val ended: Boolean get() = closed
            override fun poll(): LiveFrame? = slot.poll()
            override fun close() {
                closed = true
                slot.close()
            }
        }
    }

    override fun close() {
        closed = true
        slot.close()
    }
}
