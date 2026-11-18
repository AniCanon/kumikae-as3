package app.anicanon.kumikae.as3.simulator

import java.awt.image.BufferedImage

class LiveFrame(
    val device: String,
    val generation: Long,
    val image: BufferedImage,
    val receivedNanos: Long,
    val decodedNanos: Long,
    private var recycler: ((BufferedImage) -> Unit)? = null,
) {
    /** The sole consumer has detached this image. Any lasting copy must have its own raster. */
    fun retire() {
        recycler?.let {
            recycler = null
            it(image)
        }
    }
}

interface FrameFeed : AutoCloseable {
    /** A notification only. Consumers pull the newest picture and may coalesce notifications. */
    fun setFrameListener(listener: (() -> Unit)?) {}
    fun poll(): LiveFrame?
    val failure: Throwable?
    val ended: Boolean
}
