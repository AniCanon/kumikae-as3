package app.anicanon.kumikae.as3.simulator

import java.nio.file.Files
import java.nio.file.Path
import javax.imageio.ImageIO

class CapturedImage(val bytes: ByteArray, val size: Geometry, val capturedAt: Long, val captureMs: Long)
class BinaryFrame(val device: String, val generation: Long, val format: String, val picture: CapturedImage)

/** File capture and image inspection run outside the session lock. */
internal class SnapshotCapture(private val directory: Path, private val runner: CommandRunner) : AutoCloseable {
    private var closed = false

    @Synchronized fun capture(device: String, format: String): CapturedImage {
        check(!closed) { "Capture is closed" }
        require(format in setOf("png", "jpeg")) { "format must be png or jpeg" }
        val file = Files.createTempFile(directory, "frame-", ".$format")
        val started = System.nanoTime()
        try {
            runner.run(Commands.screenshot(device, file.toString(), format), 15_000)
            require(Files.size(file) in 1..32L * 1024 * 1024) { "Invalid screenshot size" }
            val size = ImageIO.createImageInputStream(file.toFile()).use { stream ->
                val readers = ImageIO.getImageReaders(stream)
                require(readers.hasNext()) { "Screenshot is not an image" }
                val reader = readers.next()
                try {
                    reader.input = stream
                    Geometry(reader.getWidth(0), reader.getHeight(0)).also {
                        require(it.width.toLong() * it.height <= 40_000_000) { "Screenshot is too large" }
                    }
                } finally {
                    reader.dispose()
                }
            }
            return CapturedImage(
                Files.readAllBytes(file),
                size,
                System.currentTimeMillis(),
                (System.nanoTime() - started) / 1_000_000,
            )
        } finally {
            Files.deleteIfExists(file)
        }
    }

    @Synchronized override fun close() {
        closed = true
    }
}
