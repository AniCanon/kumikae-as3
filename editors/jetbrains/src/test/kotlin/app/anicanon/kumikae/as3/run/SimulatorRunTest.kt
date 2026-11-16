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
        SimulatorRun(repository, bundle, app, { text, error -> said += text to error }, exists = { true })

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

    @Test fun `only what is printed after the launch is passed on`() {
        repository.select("A")
        repository.page =
            ConsolePage(listOf(ConsoleEntry("stdout", "an earlier run\n")), nextCursor = 9, truncated = false)
        val run = run().also { it.start() }
        // The end of the log is asked for from its start: a place past its end is refused by the service.
        assertTrue("console 0" in repository.calls)
        repository.page = ConsolePage(emptyList(), 9, truncated = false)
        run.pump()
        assertTrue("console 9" in repository.calls)
        assertTrue(said.isEmpty())
    }

    @Test fun `an app that is not on the simulator is said so, once, with what to do`() {
        repository.select("A")
        val run = run().also { it.start() }
        val refused = ConsoleEntry("stderr", "The request to open \"org.example.game\" failed.\n")
        repository.page = ConsolePage(listOf(refused, refused), nextCursor = 2, truncated = false)
        run.pump()
        val hints = said.filter { it.first.contains("is not installed on this simulator") }
        assertEquals(1, hints.size)
        assertTrue(hints.single().first.contains("org.example.game") && hints.single().first.contains("Export"))
    }

    @Test fun `with no app named, the game is built first and what was built is installed`() {
        repository.select("A")
        repository.calls.clear()
        val built = mutableListOf<String>()
        val run = SimulatorRun(repository, "org.example.game", "", { text, error -> said += text to error }) { say ->
            say("Exporting…\n", false)
            built += "build"
            "/games/out/Game.app"
        }
        run.start()
        assertEquals(listOf("build"), built)
        assertEquals(listOf("connect", "status", "install A /games/out/Game.app", "launch A org.example.game"), asked)
        assertEquals("Exporting…\n" to false, said.first())
    }

    @Test fun `a build that fails stops the run before anything is installed or launched`() {
        repository.select("A")
        repository.calls.clear()
        val run =
            SimulatorRun(repository, "org.example.game", "", { _, _ -> }) { error("The build stopped with status 65.") }
        val failure = assertFailsWith<IllegalStateException> { run.start() }
        assertEquals("The build stopped with status 65.", failure.message)
        assertTrue(asked.none { it.startsWith("install") || it.startsWith("launch") })
    }

    @Test fun `an app that is named is used as it is and nothing is built`() {
        repository.select("A")
        repository.calls.clear()
        SimulatorRun(repository, "org.example.game", "/games/Game.app", { _, _ -> }, exists = { true }) {
            error("must not build")
        }.start()
        assertEquals(listOf("connect", "status", "install A /games/Game.app", "launch A org.example.game"), asked)
    }

    @Test fun `an app that is named and is not there is built, and the run says so`() {
        repository.select("A")
        repository.calls.clear()
        val run = SimulatorRun(
            repository,
            "org.example.game",
            "/games/.build/Game.app",
            { text, error -> said += text to error },
            exists = { false },
        ) { "/games/out/Game.app" }
        run.start()
        assertEquals(listOf("connect", "status", "install A /games/out/Game.app", "launch A org.example.game"), asked)
        assertEquals(
            "There is no app at /games/.build/Game.app: building the game for the Simulator.\n" to false,
            said.first(),
        )
    }

    @Test fun `an app that is not there, with nothing to build from, says where it looked and what to do`() {
        repository.select("A")
        repository.calls.clear()
        val run =
            SimulatorRun(repository, "org.example.game", "/games/.build/Game.app", { _, _ -> }, exists = { false })
        val failure = assertFailsWith<IllegalStateException> { run.start() }
        assertEquals(
            "There is no app at /games/.build/Game.app. Build the game for the Simulator, or clear the app in this " +
                "run configuration so that Run builds it.",
            failure.message,
        )
        assertTrue(asked.none { it.startsWith("install") || it.startsWith("launch") })
    }

    @Test fun `the game is told where its scripts are edited, so that it takes a save while it runs`() {
        repository.select("A")
        repository.calls.clear()
        SimulatorRun(repository, "org.example.game", "", { _, _ -> }, sources = "/games/mine").start()
        assertEquals(listOf("connect", "status", "launch A org.example.game from /games/mine"), asked)
    }

    @Test fun `stopping ends the app`() {
        repository.select("A")
        val run = run().also { it.start() }
        run.stop()
        assertEquals("terminate A org.example.game", repository.calls.last())
    }
}
