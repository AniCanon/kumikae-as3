package app.anicanon.kumikae.as3.simulator

import java.awt.image.BufferedImage
import java.awt.image.DataBufferInt
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.nio.file.Path
import java.nio.file.StandardOpenOption

data class NativePicture(val header: NativeFrameHeader, val image: BufferedImage)

class NativeFrameReader(
    path: Path,
    private val allocate: (Int, Int) -> BufferedImage = { width, height ->
        BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB_PRE)
    },
) : AutoCloseable {
    private val channel = FileChannel.open(path, StandardOpenOption.READ)
    private val memory = try {
        require(channel.size() == NativeFrameLayout.fileBytes) { "Invalid native frame slot size" }
        channel.map(FileChannel.MapMode.READ_ONLY, 0, channel.size()).order(ByteOrder.LITTLE_ENDIAN)
    } catch (failure: Exception) {
        channel.close()
        throw failure
    }
    private var sequence = 0L

    fun poll(): NativePicture? {
        val initialLock = channel.tryLock(0, Long.MAX_VALUE, true) ?: return null
        val initial = initialLock.use { readHeader() } ?: return null
        // Allocation can collect the JVM heap. Never make the native writer wait for that pause.
        val image = allocate(initial.width, initial.height)
        val pixels = (image.raster.dataBuffer as DataBufferInt).data
        val lock = channel.tryLock(0, Long.MAX_VALUE, true) ?: return null
        lock.use {
            val header = readHeader() ?: return null
            if (header.width != image.width || header.height != image.height) return null
            memory.position(NativeFrameLayout.headerBytes)
            val source = memory.asIntBuffer()
            if (header.stride == header.width * 4) {
                source.get(pixels, 0, header.width * header.height)
            } else {
                for (row in 0 until header.height) {
                    source.position(row * (header.stride / 4))
                    source.get(pixels, row * header.width, header.width)
                }
            }
            sequence = header.sequence
            return NativePicture(header, image)
        }
    }

    /** Called only under the slot lock, including the recheck after allocating. */
    private fun readHeader(): NativeFrameHeader? {
        memory.position(0)
        if (NativeFrameLayout.read("magic", memory) == 0L) return null
        val header = NativeFrameHeader.decode(memory)
        if (header.sequence == sequence) return null
        require(header.sequence > sequence) { "Native frame sequence went backwards" }
        return header
    }

    override fun close() = channel.close()
}
