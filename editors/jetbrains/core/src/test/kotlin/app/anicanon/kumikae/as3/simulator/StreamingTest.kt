package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.IOException

class StreamingTest {
    // An EOI inside a comment is data, not the end of the image.
    private val jpeg = byteArrayOf(-1, -40, -1, -2, 0, 4, -1, -39, -1, -38, 0, 2, 1, -1, 0, 2, -1, -39)

    @Test fun `split and concatenated JPEGs retain complete images`() {
        val frames = mutableListOf<ByteArray>()
        val input = object : ByteArrayInputStream(jpeg + jpeg) {
            override fun read(bytes: ByteArray, offset: Int, length: Int) = super.read(bytes, offset, minOf(length, 1))
        }
        JpegFrames.read(input) { frames.add(it) }
        assertEquals(2, frames.size)
        frames.forEach { assertArrayEquals(jpeg, it) }
    }

    @Test fun `truncated and oversized frames fail with a message`() {
        assertThrows(IOException::class.java) {
            JpegFrames.read(ByteArrayInputStream(jpeg.dropLast(1).toByteArray())) {}
        }
        val error = assertThrows(IOException::class.java) {
            JpegFrames.read(ByteArrayInputStream(jpeg), maxBytes = 8) {}
        }
        assertTrue(error.message!!.contains("large"))
    }

    @Test fun `only the newest value is kept and closing discards it`() {
        val slot = LatestValue<Int>()
        repeat(10000) { slot.put(it) }
        assertEquals(9999, slot.poll())
        assertNull(slot.poll())
        slot.put(42)
        slot.close()
        slot.put(43)
        assertNull(slot.poll())
        assertNull(slot.take())
    }
}
