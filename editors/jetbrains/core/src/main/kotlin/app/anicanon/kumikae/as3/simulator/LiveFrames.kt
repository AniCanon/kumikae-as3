package app.anicanon.kumikae.as3.simulator

import java.awt.image.BufferedImage
import java.io.ByteArrayInputStream
import java.util.concurrent.atomic.AtomicBoolean
import javax.imageio.ImageIO

class LiveFrame(
    val device: String,
    val generation: Long,
    val image: BufferedImage,
    val receivedNanos: Long,
    val decodedNanos: Long,
)

interface FrameFeed : AutoCloseable {
    fun poll(): LiveFrame?
    val failure: Throwable?
    val ended: Boolean
}

/** Reading never waits for decode or painting. Both stages retain just the latest complete picture. */
class LiveFrames(
    private val device: String,
    private val child: StreamProcess,
    private val bind: (Geometry) -> Long?,
    private val stopped: () -> Unit = {},
) : FrameFeed {
    private class Encoded(val bytes: ByteArray, val received: Long)
    private val encoded = LatestValue<Encoded>()
    private val decoded = LatestValue<LiveFrame>()
    private val closed = AtomicBoolean()
    override val ended: Boolean get() = closed.get()

    @Volatile override var failure: Throwable? = null
        private set
    private val reader = worker("read") {
        JpegFrames.read(child.output) { encoded.put(Encoded(it, System.nanoTime())) }
        if (!closed.get()) error("idb video stream ended. ${child.errors}")
    }
    private val decoder = worker("decode") {
        var previous: ByteArray? = null
        while (!closed.get()) {
            val next = encoded.take() ?: break
            if (previous?.contentEquals(next.bytes) == true) continue
            previous = next.bytes
            val image = ImageIO.read(ByteArrayInputStream(next.bytes)) ?: error("idb returned an invalid JPEG")
            if (closed.get()) break
            val stamp = bind(Geometry(image.width, image.height)) ?: continue
            decoded.put(LiveFrame(device, stamp, image, next.received, System.nanoTime()))
        }
    }

    fun start() {
        decoder.start()
        reader.start()
    }

    override fun poll(): LiveFrame? = decoded.poll()

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        stopped()
        encoded.close()
        decoded.close()
        child.close()
        reader.interrupt()
        decoder.interrupt()
    }

    private fun worker(name: String, work: () -> Unit) = Thread({
        try {
            work()
        } catch (error: Exception) {
            if (!closed.get()) {
                failure = error
            }
        } finally {
            close()
        }
    }, "kumikae-video-$name").apply { isDaemon = true }
}
