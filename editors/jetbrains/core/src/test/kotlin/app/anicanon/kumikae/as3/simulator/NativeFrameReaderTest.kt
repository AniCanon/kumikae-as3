package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import java.io.RandomAccessFile
import java.nio.file.Files
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class NativeFrameReaderTest {
    @Test fun allocationAllowsPublicationAndThenReadsTheNewestHeader() {
        val file = Files.createTempFile("native-allocation", ".slot")
        try {
            RandomAccessFile(file.toFile(), "rw").use { writer ->
                writer.setLength(NativeFrameLayout.fileBytes)
                val header = NativeFrameHeader(1, 1, 1, 4, 1, 2, 1, 1, 1000)
                writer.write(header.encode())
                writer.write(byteArrayOf(0, 0, -1, -1))
                NativeFrameReader(file) { width, height ->
                    // Allocation may trigger GC. The writer must be free to publish meanwhile.
                    assertNotNull(writer.channel.tryLock()).use {
                        writer.seek(0)
                        writer.write(header.copy(sequence = 2).encode())
                        writer.write(byteArrayOf(-1, 0, 0, -1))
                    }
                    java.awt.image.BufferedImage(width, height, java.awt.image.BufferedImage.TYPE_INT_ARGB_PRE)
                }.use { reader ->
                    val newest = assertNotNull(reader.poll())
                    assertEquals(2, newest.header.sequence)
                    assertEquals(0xff0000ff.toInt(), newest.image.getRGB(0, 0))
                }
            }
        } finally {
            Files.delete(file)
        }
    }

    @Test fun copiesBGRAWithPaddingAndRetainsOnlyNewestSequence() {
        val file = Files.createTempFile("native-frame", ".slot")
        try {
            RandomAccessFile(file.toFile(), "rw").use { writer ->
                writer.setLength(NativeFrameLayout.fileBytes)
                NativeFrameReader(file).use { reader ->
                    assertNull(reader.poll())
                    val header = NativeFrameHeader(1, 1, 2, 8, 1, 2, 1, 2, 1000)
                    writer.write(header.encode())
                    writer.write(byteArrayOf(0, 0, -1, -1, 33, 33, 33, 33, 0, -1, 0, -1, 33, 33, 33, 33))
                    writer.seek(0)
                    writer.write(header.copy(sequence = 3).encode())
                    val newest = assertNotNull(reader.poll())
                    assertEquals(3, newest.header.sequence)
                    assertEquals(0xffff0000.toInt(), newest.image.getRGB(0, 0))
                    assertEquals(0xff00ff00.toInt(), newest.image.getRGB(0, 1))
                    assertNull(reader.poll())
                    writer.seek(0)
                    writer.write(ByteArray(NativeFrameLayout.headerBytes))
                    assertNull(reader.poll(), "An interrupted writer must leave no valid frame")
                }
            }
        } finally {
            Files.deleteIfExists(file)
        }
    }
}
