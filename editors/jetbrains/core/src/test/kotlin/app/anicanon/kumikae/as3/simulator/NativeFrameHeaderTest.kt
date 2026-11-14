package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class NativeFrameHeaderTest {
    private val sample = NativeFrameHeader(7, 2, 3, 8, 123, 456, 6, 9, 3000)

    @Test fun roundTripUsesLittleEndian() {
        val bytes = sample.encode()
        assertEquals(64, bytes.size)
        val golden = checkNotNull(javaClass.getResourceAsStream("/frame-header-golden.hex")).bufferedReader().use {
            it.readText().trim()
        }
        assertEquals(golden, java.util.HexFormat.of().formatHex(bytes))
        assertEquals(sample, NativeFrameHeader.decode(bytes))
        assertContentEquals(byteArrayOf(7, 0, 0, 0, 0, 0, 0, 0), bytes.copyOfRange(8, 16))
    }

    @Test fun refusesMalformedGeometry() {
        listOf(
            sample.copy(width = 0),
            sample.copy(height = -1),
            sample.copy(stride = 7),
            sample.copy(width = Int.MAX_VALUE),
            sample.copy(densityMilli = 0),
        ).forEach { bad -> assertFailsWith<IllegalArgumentException> { bad.encode() } }
    }

    @Test fun refusesTruncationUnknownVersionAndFormat() {
        val bytes = sample.encode()
        assertFailsWith<IllegalArgumentException> { NativeFrameHeader.decode(bytes.copyOf(63)) }
        listOf(0, 4, 28).forEach { offset ->
            val bad = bytes.copyOf()
            bad[offset] = (bad[offset].toInt() xor 255).toByte()
            assertFailsWith<IllegalArgumentException> { NativeFrameHeader.decode(bad) }
        }
    }
}
