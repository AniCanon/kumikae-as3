package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import java.nio.ByteBuffer
import java.nio.ByteOrder

internal object NativeFrameLayout {
    private val data = checkNotNull(javaClass.getResourceAsStream("/frame-layout.json")) {
        "The bridge frame-layout resource is missing. Rebuild the plugin."
    }.reader().use { JsonParser.parseReader(it).asJsonObject }
    val headerBytes = data["headerBytes"].asInt
    val maxPayloadBytes = data["maxPayloadBytes"].asInt
    val maxDimension = data["maxDimension"].asInt
    val fileBytes = headerBytes.toLong() + maxPayloadBytes
    val constants = mapOf(
        "magic" to data["magic"].asLong,
        "version" to data["version"].asLong,
        "pixelFormat" to data["pixelFormat"].asLong,
        "flags" to 1L,
    )

    private fun field(name: String): Pair<Int, Int> {
        val field = data.getAsJsonObject("fields")[name].asJsonArray
        val offset = field[0].asInt
        val bytes = field[1].asInt
        require(bytes in listOf(4, 8) && offset >= 0 && offset + bytes <= headerBytes) {
            "Invalid frame layout field: $name"
        }
        return offset to bytes
    }

    fun read(name: String, data: ByteBuffer): Long {
        require(data.remaining() >= headerBytes) { "Truncated native frame header" }
        val (offset, size) = field(name)
        data.order(ByteOrder.LITTLE_ENDIAN)
        return if (size == 8) data.getLong(offset) else Integer.toUnsignedLong(data.getInt(offset))
    }

    fun write(name: String, value: Long, data: ByteBuffer) {
        val (offset, size) = field(name)
        require(value >= 0 && (size == 8 || value <= 0xffff_ffffL)) { "Native frame field is too large: $name" }
        data.order(ByteOrder.LITTLE_ENDIAN)
        if (size == 8) data.putLong(offset, value) else data.putInt(offset, value.toInt())
    }
}
