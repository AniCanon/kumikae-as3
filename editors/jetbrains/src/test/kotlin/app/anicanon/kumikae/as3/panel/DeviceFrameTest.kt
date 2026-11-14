package app.anicanon.kumikae.as3.panel

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DeviceFrameTest {
    private val frame =
        DeviceFrame(screenWidth = 1206, screenHeight = 2622, viewWidth = 400, viewHeight = 800, margin = 18)

    @Test
    fun `the device with its bezel fits inside the margin`() {
        val screen = frame.screen
        assertTrue(screen.x > 18 && screen.y > 18)
        assertTrue(screen.x + screen.width < 400 - 18 && screen.y + screen.height < 800 - 18)
        assertEquals(1206.0 / 2622.0, screen.width / screen.height, 1e-9)
    }

    @Test
    fun `a point on the screen is the pixel under it`() {
        val screen = frame.screen
        assertEquals(0 to 0, frame.pixel(screen.x, screen.y))
        assertEquals(603 to 1311, frame.pixel(screen.centerX, screen.centerY))
        assertEquals(1205 to 2621, frame.pixel(screen.maxX - 0.01, screen.maxY - 0.01))
    }

    @Test
    fun `a point on the bezel or outside the device is no pixel`() {
        assertNull(frame.pixel(frame.screen.x - 1, frame.screen.centerY))
        assertNull(frame.pixel(5.0, 5.0))
    }

    @Test
    fun `a view with no room gives no pixel`() {
        val none = DeviceFrame(1206, 2622, viewWidth = 20, viewHeight = 20, margin = 18)
        assertEquals(0.0, none.scale, 0.0)
        assertNull(none.pixel(10.0, 10.0))
    }

    @Test
    fun `a landscape picture keeps its shape`() {
        val wide = DeviceFrame(2622, 1206, viewWidth = 900, viewHeight = 400, margin = 18)
        assertEquals(2622.0 / 1206.0, wide.screen.width / wide.screen.height, 1e-9)
        assertEquals(1311 to 603, wide.pixel(wide.screen.centerX, wide.screen.centerY))
    }
}
