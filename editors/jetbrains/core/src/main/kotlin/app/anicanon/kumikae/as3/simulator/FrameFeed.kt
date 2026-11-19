package app.anicanon.kumikae.as3.simulator

import java.awt.image.BufferedImage
import java.util.concurrent.atomic.AtomicReference

class LiveFrame(
    val device: String,
    val generation: Long,
    val image: BufferedImage,
    val receivedNanos: Long,
    val decodedNanos: Long,
    recycler: ((BufferedImage) -> Unit)? = null,
) {
    private val pendingRecycler = AtomicReference(recycler)

    /** The sole consumer has detached this image. Any lasting copy must have its own raster. */
    fun retire() {
        pendingRecycler.getAndSet(null)?.invoke(image)
    }
}

interface FrameFeed : AutoCloseable {
    /** A notification only. Consumers pull the newest picture and may coalesce notifications. */
    fun setFrameListener(listener: (() -> Unit)?) {}
    fun poll(): LiveFrame?
    val failure: Throwable?
    val ended: Boolean
}
