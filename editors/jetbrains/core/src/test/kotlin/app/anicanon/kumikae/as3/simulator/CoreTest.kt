package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test

class CoreTest {
    @Test fun `only available iOS devices survive parsing`() {
        val json = """{"devices":{
          "com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
            {"udid":"test-phone","name":"Phone","isAvailable":true,"state":"Booted"},
            {"udid":"missing","name":"Missing","isAvailable":false,"state":"Shutdown"}],
          "com.apple.CoreSimulator.SimRuntime.tvOS-26-4":[
            {"udid":"tv","name":"TV","isAvailable":true,"state":"Booted"}]}}"""
        assertEquals(listOf(Device("test-phone", "Phone", "iOS 26.4", "Booted")), Devices.parse(json))
        assertEquals(emptyList<Device>(), Devices.parse("""{"devices":{}}"""))
    }

    @Test fun `unknown JSON fields are harmless but malformed device lists fail`() {
        assertThrows(IllegalArgumentException::class.java) { Devices.parse("{}") }
        assertThrows(IllegalArgumentException::class.java) { Devices.parse("{broken") }
        assertThrows(IllegalArgumentException::class.java) { Devices.parse("""{"devices":[]}""") }
    }

    @Test fun `letterbox mapping ignores margins and right and bottom edges`() {
        val g = Geometry(1200, 2400)
        assertEquals(Pixel(600, 1200), g.fromView(300.0, 300.0, 600, 600))
        assertNull(g.fromView(149.0, 300.0, 600, 600))
        assertEquals(Pixel(0, 0), g.fromView(150.0, 0.0, 600, 600))
        assertNull(g.fromView(450.0, 300.0, 600, 600))
        assertNull(g.fromView(300.0, 600.0, 600, 600))
        assertNull(g.fromView(Double.NaN, 1.0, 600, 600))
        assertNull(g.fromView(1.0, 1.0, 0, 600))
    }

    @Test fun `landscape and retina use image pixels rather than host screen scale`() {
        val g = Geometry(2400, 1200)
        assertEquals(Pixel(1200, 600), g.fromView(300.0, 300.0, 600, 600))
        assertEquals(Pixel(400, 200), g.point(1200.0, 600.0, 3.0))
        for (bad in listOf(-1.0, 2400.0, Double.NaN, Double.POSITIVE_INFINITY)) {
            assertThrows(IllegalArgumentException::class.java) { g.point(bad, 0.0, 3.0) }
        }
        assertThrows(IllegalArgumentException::class.java) { g.point(1.0, 1.0, 0.0) }
        assertThrows(IllegalArgumentException::class.java) { Geometry(0, 100) }
    }

    @Test fun `commands target one device and keep paths and text as single arguments`() {
        assertEquals(listOf("/usr/bin/xcrun", "simctl", "list", "-j", "devices", "available"), Commands.list())
        assertEquals(listOf("/usr/bin/xcrun", "simctl", "bootstatus", "test-phone", "-b"), Commands.boot("test-phone"))
        assertEquals(
            listOf("/usr/bin/xcrun", "simctl", "io", "test-phone", "screenshot", "--type=png", "/tmp/a b.png"),
            Commands.screenshot("test-phone", "/tmp/a b.png", "png"),
        )
        assertEquals(
            listOf("/usr/bin/xcrun", "simctl", "install", "test-phone", "/tmp/a b.app"),
            Commands.install("test-phone", "/tmp/a b.app"),
        )
        assertEquals(
            listOf(
                "/usr/bin/xcrun",
                "simctl",
                "launch",
                "--terminate-running-process",
                "--console",
                "test-phone",
                "org.kumikae.as3test",
            ),
            Commands.launch("test-phone", "org.kumikae.as3test"),
        )
    }
}
