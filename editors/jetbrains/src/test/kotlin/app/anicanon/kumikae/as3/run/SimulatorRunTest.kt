package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.panel.ConsoleEntry
import app.anicanon.kumikae.as3.panel.ConsolePage
import app.anicanon.kumikae.as3.panel.FakeSimulatorRepository
import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SimulatorRunTest {
    private val repository = FakeSimulatorRepository()
    private val said = mutableListOf<Pair<String, Boolean>>()

    /** What was asked of the Simulator, leaving out the reads of its console. */
    private val asked get() = repository.calls.filterNot { it.startsWith("console") }

    private fun run(bundle: String = "org.example.game", app: String = "") =
        SimulatorRun(repository, bundle, app) { text, error -> said += text to error }

    @Test fun `the app is launched on the device the panel shows`() {
        repository.select("A")
        repository.calls.clear()
        run().start()
        assertEquals(listOf("connect", "status", "launch A org.example.game"), asked)
    }

    @Test fun `with no device chosen, the first that is running is chosen`() {
        run().start()
        assertEquals(listOf("connect", "status", "devices", "select A", "launch A org.example.game"), asked)
    }

    @Test fun `an app that is given is installed before it is launched`() {
        repository.select("A")
        repository.calls.clear()
        run(app = " /games/Game.app ").start()
        assertEquals(
            listOf("connect", "status", "install A /games/Game.app", "launch A org.example.game"),
            asked,
        )
    }

    @Test fun `with nothing running there is nothing to launch on, and the reason says what to do`() {
        repository.listed = listOf(repository.tablet)
        val failure = assertFailsWith<IllegalStateException> { run().start() }
        assertTrue(failure.message!!.contains("No simulator is running"), failure.message)
        assertTrue(repository.calls.none { it.startsWith("launch") })
    }

    @Test fun `a bundle identifier is required`() {
        val failure = assertFailsWith<IllegalStateException> { run(bundle = "  ").start() }
        assertTrue(failure.message!!.contains("bundle identifier"))
    }

    @Test fun `output is passed on as it is read, the app's errors marked, until the app ends`() {
        repository.select("A")
        val run = run().also { it.start() }
        repository.page = ConsolePage(
            listOf(ConsoleEntry("stdout", "hello\n"), ConsoleEntry("stderr", "oops\n"), ConsoleEntry("session", "x\n")),
            nextCursor = 3,
            truncated = false,
        )
        assertNull(run.pump())
        assertEquals(listOf("hello\n" to false, "oops\n" to true, "x\n" to false), said)
        repository.page = ConsolePage(emptyList(), 3, truncated = false, running = false, exitCode = 7)
        assertEquals(7, run.pump())
        assertTrue("console 3" in repository.calls)
    }

    @Test fun `stopping ends the app`() {
        repository.select("A")
        val run = run().also { it.start() }
        run.stop()
        assertEquals("terminate A org.example.game", repository.calls.last())
    }
}
