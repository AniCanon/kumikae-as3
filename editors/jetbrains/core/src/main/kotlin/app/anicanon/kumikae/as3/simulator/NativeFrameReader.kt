package app.anicanon.kumikae.as3.simulator

import java.awt.image.BufferedImage
import java.awt.image.DataBufferInt
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.nio.file.Path
import java.nio.file.StandardOpenOption

internal data class NativePicture(val header: NativeFrameHeader, val image: BufferedImage)

internal class NativeFrameReader(path: Path) : AutoCloseable {
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
        val lock = channel.tryLock(0, Long.MAX_VALUE, true) ?: return null
        lock.use {
            memory.position(0)
            if (NativeFrameLayout.read("magic", memory) == 0L) return null
            val header = NativeFrameHeader.decode(memory)
            if (header.sequence == sequence) return null
            require(header.sequence > sequence) { "Native frame sequence went backwards" }
            val image = BufferedImage(header.width, header.height, BufferedImage.TYPE_INT_ARGB_PRE)
            val pixels = (image.raster.dataBuffer as DataBufferInt).data
            for (row in 0 until header.height) {
                memory.position(NativeFrameLayout.headerBytes + row * header.stride)
                memory.asIntBuffer().get(pixels, row * header.width, header.width)
            }
            sequence = header.sequence
            return NativePicture(header, image)
        }
    }

    override fun close() = channel.close()
}
