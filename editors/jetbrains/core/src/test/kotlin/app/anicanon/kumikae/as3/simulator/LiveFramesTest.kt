package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.awt.image.BufferedImage
import java.io.ByteArrayOutputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import javax.imageio.ImageIO

class LiveFramesTest {
    @Test fun `a broken byte stream reports failure and invalidates its geometry`() {
        val pipe = PipedInputStream(65536)
        val writer = PipedOutputStream(pipe)
        val closed = CountDownLatch(1)
        val bound = CountDownLatch(1)
        val invalidated = CountDownLatch(1)
        val child = object : StreamProcess {
            override val output = pipe
            override val input = ByteArrayOutputStream()
            override val errors = ""
            override fun close() {
                pipe.close()
                closed.countDown()
            }
        }
        val frame = ByteArrayOutputStream().also {
            ImageIO.write(BufferedImage(4, 8, BufferedImage.TYPE_INT_RGB), "jpeg", it)
        }.toByteArray()
        val geometry = FrameGeometry()
        val feed = LiveFrames("phone", child, bind = {
            geometry.record(it).also { bound.countDown() }
        }, stopped = {
            geometry.invalidate()
            invalidated.countDown()
        })
        feed.start()
        try {
            writer.write(frame)
            writer.flush()
            assertTrue(bound.await(2, TimeUnit.SECONDS))
            val before = geometry.generation
            writer.write(byteArrayOf(1, 2, 3))
            writer.flush()
            assertTrue(invalidated.await(2, TimeUnit.SECONDS))
            assertTrue(closed.await(2, TimeUnit.SECONDS))
            assertNotNull(feed.failure)
            assertTrue(feed.ended)
            assertEquals(before + 1, geometry.generation)
        } finally {
            feed.close()
            writer.close()
        }
    }
}
