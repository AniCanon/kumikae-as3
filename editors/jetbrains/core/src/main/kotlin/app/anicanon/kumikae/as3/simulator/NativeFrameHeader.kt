package app.anicanon.kumikae.as3.simulator

import java.nio.ByteBuffer

data class NativeFrameHeader(
    val sequence: Long,
    val width: Int,
    val height: Int,
    val stride: Int,
    val observedNanos: Long,
    val writtenNanos: Long,
    val nativeWidth: Int,
    val nativeHeight: Int,
    val densityMilli: Int,
) {
    private fun validate() {
        val max = NativeFrameLayout.maxDimension
        require(sequence > 0 && listOf(width, height, nativeWidth, nativeHeight).all { it in 1..max }) {
            "Invalid native frame dimensions"
        }
        require(
            stride >= width * 4 && stride <= max * 4 && stride % 4 == 0 &&
                stride.toLong() * height <= NativeFrameLayout.maxPayloadBytes,
        ) { "Invalid native frame stride" }
        require(densityMilli in 1000..10000 && observedNanos > 0 && writtenNanos >= observedNanos) {
            "Invalid native frame density or timestamp"
        }
    }

    fun encode(): ByteArray {
        validate()
        val data = ByteBuffer.allocate(NativeFrameLayout.headerBytes)
        val values = NativeFrameLayout.constants + mapOf(
            "sequence" to sequence, "width" to width.toLong(), "height" to height.toLong(),
            "stride" to stride.toLong(), "observedNanos" to observedNanos, "writtenNanos" to writtenNanos,
            "nativeWidth" to nativeWidth.toLong(), "nativeHeight" to nativeHeight.toLong(),
            "densityMilli" to densityMilli.toLong(),
        )
        values.forEach { (name, value) -> NativeFrameLayout.write(name, value, data) }
        return data.array()
    }

    companion object {
        fun decode(bytes: ByteArray): NativeFrameHeader = decode(ByteBuffer.wrap(bytes))

        internal fun decode(data: ByteBuffer): NativeFrameHeader {
            fun read(name: String) = NativeFrameLayout.read(name, data)
            fun integer(name: String): Int {
                val value = read(name)
                require(value in 0..Int.MAX_VALUE) { "Native frame $name is too large" }
                return value.toInt()
            }
            NativeFrameLayout.constants.forEach { (name, expected) ->
                require(read(name) == expected) { "Unsupported native frame $name" }
            }
            return NativeFrameHeader(
                read("sequence"), integer("width"), integer("height"), integer("stride"),
                read("observedNanos"), read("writtenNanos"), integer("nativeWidth"), integer("nativeHeight"),
                integer("densityMilli"),
            ).also { it.validate() }
        }
    }
}
