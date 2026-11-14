package app.anicanon.kumikae.as3.simulator

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream

/** Reads markers and segment lengths; an embedded thumbnail must not terminate the outer JPEG. */
object JpegFrames {
    fun read(source: InputStream, maxBytes: Int = 8 * 1024 * 1024, receive: (ByteArray) -> Unit) {
        val input = source.buffered()
        while (true) {
            val first = input.read()
            if (first < 0) return
            val bytes = ByteArrayOutputStream()
            fun next(): Int {
                val value = input.read()
                if (value < 0) throw IOException("Truncated JPEG stream")
                if (bytes.size() >= maxBytes) throw IOException("JPEG frame is too large")
                bytes.write(value)
                return value
            }
            bytes.write(first)
            if (first != 0xff || next() != 0xd8) throw IOException("Stream is not MJPEG")
            var entropy = false
            while (true) {
                var prefix = next()
                if (entropy && prefix != 0xff) continue
                if (prefix != 0xff) throw IOException("Invalid JPEG marker")
                do {
                    prefix = next()
                } while (prefix == 0xff)
                if (entropy && (prefix == 0 || prefix in 0xd0..0xd7)) continue
                if (prefix == 0xd9) break
                if (prefix == 0xd8 || prefix == 0) throw IOException("Invalid JPEG segment")
                val length = (next() shl 8) or next()
                if (length < 2) throw IOException("Invalid JPEG segment length")
                repeat(length - 2) { next() }
                entropy = prefix == 0xda
            }
            receive(bytes.toByteArray())
        }
    }
}
