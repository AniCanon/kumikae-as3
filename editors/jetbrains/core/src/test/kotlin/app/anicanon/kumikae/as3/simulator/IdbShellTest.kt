package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream

class IdbShellTest {
    private class Child(replies: String) : StreamProcess {
        override val output = ByteArrayInputStream(replies.toByteArray())
        override val input = ByteArrayOutputStream()
        override val errors = "test error"
        var closed = false
        override fun close() {
            closed = true
        }
    }

    @Test fun `one shell sends multiple acknowledged commands and quotes text`() {
        val child = Child("SUCCESS=1\nSUCCESS=1\n")
        IdbShell(child).use {
            it.send("tap", listOf("20", "30"))
            it.send("text", listOf("it's fine; $(ignored)"))
        }
        assertEquals("ui tap '20' '30'\nui text -- 'it'\"'\"'s fine; $(ignored)'\n", child.input.toString())
        assertTrue(child.closed)
    }

    @Test fun `failed input is reported and never retried`() {
        val child = Child("SUCCESS=0\n")
        IdbShell(child).use {
            val error = assertThrows(SimulatorException::class.java) { it.send("tap", listOf("1", "2")) }
            assertTrue(error.message!!.contains("test error"))
            assertEquals(1, child.input.toString().lines().count { line -> line.startsWith("ui") })
        }
    }

    @Test fun `a closed shell rejects later input immediately without writing it`() {
        val child = Child("SUCCESS=1\n")
        val shell = IdbShell(child)
        shell.close()
        assertThrows(SimulatorException::class.java) { shell.send("tap", listOf("1", "2")) }
        assertEquals("", child.input.toString())
    }

    @Test fun `dead input closes the child and reports uncertain completion`() {
        val child = Child("")
        IdbShell(child).use {
            assertThrows(SimulatorException::class.java) { it.send("tap", listOf("1", "2")) }
            assertTrue(child.closed)
        }
    }
}
