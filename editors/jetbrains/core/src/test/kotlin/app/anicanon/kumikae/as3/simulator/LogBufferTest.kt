package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class LogBufferTest {
    @Test fun `logs have stable cursors and report discarded history`() {
        val log = LogBuffer(10)
        log.append("stdout", "123456")
        val first = log.read(0)
        assertEquals(1L, first.nextCursor)
        assertFalse(first.truncated)
        log.append("stderr", "abcdef")
        val second = log.read(0)
        assertTrue(second.truncated)
        assertEquals("abcdef", second.entries.single().text)
        assertEquals("stderr", second.entries.single().stream)
        assertEquals(2L, second.nextCursor)
        assertTrue(log.read(2).entries.isEmpty())
        assertThrows(IllegalArgumentException::class.java) { log.read(3) }
    }

    @Test fun `one oversized write stays bounded`() {
        val log = LogBuffer(10)
        log.append("stdout", "abcdefghijklmnopqrstuvwxyz")
        assertEquals("qrstuvwxyz", log.read(0).entries.single().text)
        assertTrue(log.read(0).truncated)
    }
}
