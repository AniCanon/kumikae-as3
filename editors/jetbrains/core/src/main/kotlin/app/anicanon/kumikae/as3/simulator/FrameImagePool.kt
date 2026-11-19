package app.anicanon.kumikae.as3.simulator

import java.awt.image.BufferedImage
import java.util.concurrent.atomic.AtomicReference

/** Only superseded pending frames and images detached on the EDT may be returned here. */
class FrameImagePool(private val imageType: Int) {
    private val retired = AtomicReference<BufferedImage?>()
    fun acquire(width: Int, height: Int): BufferedImage {
        val available = retired.getAndSet(null)
        if (available != null && available.width == width && available.height == height) return available
        return BufferedImage(width, height, imageType)
    }

    fun release(image: BufferedImage) {
        retired.set(image)
    }
}
