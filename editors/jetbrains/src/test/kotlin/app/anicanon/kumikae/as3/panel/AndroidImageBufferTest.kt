package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.FrameImagePool
import org.junit.Test
import java.awt.image.BufferedImage
import kotlin.test.assertNotSame
import kotlin.test.assertSame

class AndroidImageBufferTest {
    @Test fun onlyRetiredMatchingPicturesAreReused() {
        val buffer = FrameImagePool(BufferedImage.TYPE_INT_RGB)
        val visible = buffer.acquire(4, 8)
        val pending = buffer.acquire(4, 8)
        assertNotSame(visible, pending)
        buffer.release(visible)
        assertSame(visible, buffer.acquire(4, 8))
        buffer.release(pending)
        assertNotSame(pending, buffer.acquire(8, 4))
    }
}
