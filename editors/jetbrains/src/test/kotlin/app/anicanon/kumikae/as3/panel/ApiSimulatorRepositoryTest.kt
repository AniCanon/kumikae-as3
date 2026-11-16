package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Geometry
import app.anicanon.kumikae.as3.simulator.Pixel
import com.google.gson.JsonElement
import com.google.gson.JsonParser
import org.junit.jupiter.api.Test
import java.awt.image.BufferedImage
import java.io.ByteArrayOutputStream
import java.util.Base64
import javax.imageio.ImageIO
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ApiSimulatorRepositoryTest {
    private class FakeService(private val replies: Map<String, String>) : SimulatorService {
        override fun enableInput() = true

        val calls = mutableListOf<Pair<String, Map<String, Any>>>()
        var closed = false

        override fun start() = "/state/session.json"

        override fun call(method: String, params: Map<String, Any>): JsonElement {
            calls += method to params
            return JsonParser.parseString(replies.getValue(method))
        }

        override fun close() {
            closed = true
        }
    }

    private val device = """{"id":"A","name":"Phone","runtime":"iOS 26.4","state":"Booted"}"""
    private val status =
        """{"device":$device,"generation":3,"inputEnabled":true,"pixelsPerPoint":3.0,"inputNotice":"native helper","launch":null}"""

    private fun repository(vararg replies: Pair<String, String>) =
        FakeService(replies.toMap()).let { it to ApiSimulatorRepository(it) }

    @Test fun `connect starts the service and close stops it`() {
        val (service, repository) = repository()
        assertEquals("/state/session.json", repository.connect())
        repository.close()
        assertTrue(service.closed)
    }

    @Test fun `devices become typed values with a readable label`() {
        val (service, repository) = repository("devices" to "[$device]")
        val devices = repository.devices()
        assertEquals(listOf(SimulatorDevice("A", "Phone", "iOS 26.4", "Booted")), devices)
        assertEquals("Phone · iOS 26.4 · Booted", devices.single().label)
        assertEquals(listOf("devices" to emptyMap<String, Any>()), service.calls)
    }

    @Test fun `status carries the selected device, input permission and notice`() {
        val (_, selected) = repository("status" to status)
        assertEquals(
            SimulatorStatus(SimulatorDevice("A", "Phone", "iOS 26.4", "Booted"), true, "native helper"),
            selected.status(),
        )
        val (_, empty) = repository(
            "status" to """{"device":null,"generation":0,"inputEnabled":false,"inputNotice":"Viewer mode."}""",
        )
        assertEquals(SimulatorStatus(null, false, "Viewer mode."), empty.status())
    }

    @Test fun `select and boot name the device and return the new status`() {
        val (service, repository) = repository("select" to status, "boot" to status)
        assertEquals("A", repository.select("A").device?.id)
        assertEquals("A", repository.boot("A").device?.id)
        val named = mapOf<String, Any>("device" to "A")
        assertEquals(listOf("select" to named, "boot" to named), service.calls)
    }

    @Test fun `a launch names the folder the scripts are edited in, when there is one`() {
        val (service, repository) = repository("launch" to """{"state":"starting"}""")
        repository.launch("A", "org.example.game", "/games/mine")
        assertEquals(
            listOf<Pair<String, Map<String, Any>>>(
                "launch" to mapOf("device" to "A", "bundle" to "org.example.game", "sources" to "/games/mine"),
            ),
            service.calls,
        )
    }

    @Test fun `install and launch send the path and the bundle`() {
        val (service, repository) = repository(
            "install" to """{"installed":true}""",
            "launch" to """{"state":"starting"}""",
        )
        repository.install("A", "/apps/game.app")
        repository.launch("A", "org.example.game")
        assertEquals(
            listOf<Pair<String, Map<String, Any>>>(
                "install" to mapOf("device" to "A", "path" to "/apps/game.app"),
                "launch" to mapOf("device" to "A", "bundle" to "org.example.game"),
            ),
            service.calls,
        )
    }

    @Test fun `a screenshot is requested as JPEG and decoded`() {
        val bytes = ByteArrayOutputStream().also {
            ImageIO.write(BufferedImage(4, 2, BufferedImage.TYPE_INT_RGB), "jpeg", it)
        }.toByteArray()
        val data = Base64.getEncoder().encodeToString(bytes)
        val (service, repository) = repository(
            "screenshot" to
                """{"device":"A","width":4,"height":2,"generation":7,"format":"jpeg","data":"$data",""" +
                """"capturedAt":1,"captureMs":435}""",
        )
        val frame = repository.screenshot("A")
        assertEquals(
            listOf("A", 7L, Geometry(4, 2), 435L),
            listOf(frame.device, frame.generation, frame.geometry, frame.captureMs),
        )
        assertEquals(mapOf<String, Any>("device" to "A", "format" to "jpeg"), service.calls.single().second)
    }

    @Test fun `a screenshot that is not an image is an error`() {
        val (_, repository) = repository(
            "screenshot" to """{"device":"A","generation":7,"data":"AAAA","captureMs":1}""",
        )
        assertFailsWith<IllegalStateException> { repository.screenshot("A") }
    }

    @Test fun `a console page keeps its entries, next cursor and truncation mark`() {
        val (service, repository) = repository(
            "log" to
                """{"page":{"entries":[{"cursor":5,"stream":"stdout","text":"hello\n"}],"nextCursor":6,""" +
                """"truncated":true},"launch":null}""",
        )
        assertEquals(ConsolePage(listOf(ConsoleEntry("stdout", "hello\n")), 6, true), repository.consolePage(5))
        assertEquals(mapOf<String, Any>("cursor" to 5L), service.calls.single().second)
    }

    @Test fun `a console page says whether the launched app is still running, and how it ended`() {
        val page = """{"entries":[],"nextCursor":2,"truncated":false}"""
        val (_, running) = repository(
            "log" to """{"page":$page,"launch":{"bundle":"a.b","running":true,"exitCode":null}}""",
        )
        assertEquals(true to null, running.consolePage(0).let { it.running to it.exitCode })
        val (_, ended) = repository(
            "log" to """{"page":$page,"launch":{"bundle":"a.b","running":false,"exitCode":3}}""",
        )
        assertEquals(false to 3, ended.consolePage(0).let { it.running to it.exitCode })
    }

    @Test fun `gestures quote the frame geometry and clamp a swipe to the protocol limits`() {
        val (service, repository) = repository("tap" to """{"sent":true}""", "swipe" to """{"sent":true}""")
        val geometry = Geometry(100, 200)
        repository.input("A", SimulatorInput.Tap(7, geometry, Pixel(10, 20)))
        repository.input("A", SimulatorInput.Swipe(7, geometry, Pixel(10, 20), Pixel(30, 40), 9000))
        val tap = mapOf<String, Any>(
            "device" to "A",
            "generation" to 7L,
            "width" to 100,
            "height" to 200,
            "x" to 10,
            "y" to 20,
        )
        assertEquals(tap, service.calls[0].second)
        assertEquals(
            tap + mapOf<String, Any>("endX" to 30, "endY" to 40, "durationMs" to 5000L),
            service.calls[1].second,
        )
        assertEquals(listOf("tap", "swipe"), service.calls.map { it.first })
    }

    @Test fun `text, buttons and rotation use the protocol names`() {
        val sent = """{"sent":true}"""
        val (service, repository) = repository("text" to sent, "button" to sent, "rotate" to sent)
        repository.input("A", SimulatorInput.Text("hi"))
        repository.input("A", SimulatorInput.Press(HardwareButton.LOCK))
        repository.input("A", SimulatorInput.Rotate(Orientation.LANDSCAPE_LEFT))
        assertEquals(
            listOf<Pair<String, Map<String, Any>>>(
                "text" to mapOf("device" to "A", "text" to "hi"),
                "button" to mapOf("device" to "A", "button" to "lock"),
                "rotate" to mapOf("device" to "A", "orientation" to "landscape_left"),
            ),
            service.calls,
        )
        assertNull(service.calls.firstOrNull { it.first == "status" })
    }
}
