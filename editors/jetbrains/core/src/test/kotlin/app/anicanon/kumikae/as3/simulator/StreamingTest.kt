package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class StreamingTest {
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
