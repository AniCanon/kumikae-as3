package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import java.nio.file.Files

class LiveSessionTest {
    private val runner = CommandRunner { _, _ ->
        """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
        {"udid":"phone","name":"Phone","state":"Booted","isAvailable":true}]}}"""
    }

    @Test fun `live pictures require the same consent as input`() {
        val dir = Files.createTempDirectory("live-test")
        try {
            SimulatorSession(dir, runner).use { session ->
                session.dispatch("select", JsonParser.parseString("""{"device":"phone"}""").asJsonObject)
                val error = assertThrows(SimulatorException::class.java) { session.openFrames("phone") }
                assertEquals("INPUT_DISABLED", error.code)
            }
        } finally {
            Files.delete(dir)
        }
    }

    @Test fun `scaled frame coordinates use the native size and reject unknown dimensions`() {
        val geometry = FrameGeometry()
        val full = Geometry(1206, 2622)
        val small = Geometry(398, 865)
        val stamp = geometry.record(full, small)
        assertEquals(Pixel(201, 437), geometry.point(stamp, small, 199.0, 432.5, 3.0))
        assertEquals(stamp, geometry.record(full))
        assertEquals(Pixel(201, 437), geometry.point(stamp, small, 199.0, 432.5, 3.0))
        assertThrows(SimulatorException::class.java) { geometry.point(stamp, Geometry(2, 4), 1.0, 2.0, 3.0) }
        geometry.invalidate()
        assertThrows(SimulatorException::class.java) { geometry.point(stamp, small, 1.0, 2.0, 3.0) }
    }

    @Test fun `rotated native geometry rejects old frames even when the display size was seen before`() {
        val geometry = FrameGeometry()
        val stamp = geometry.record(Geometry(1206, 2622), Geometry(398, 865))
        geometry.record(Geometry(2622, 1206), Geometry(865, 398))
        assertThrows(SimulatorException::class.java) {
            geometry.point(stamp, Geometry(398, 865), 1.0, 2.0, 3.0)
        }
    }
}
