package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import java.io.RandomAccessFile
import java.nio.file.Files
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class NativeFrameReaderTest {
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
