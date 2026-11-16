package app.anicanon.kumikae.as3.simulator

import java.awt.image.BufferedImage

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
