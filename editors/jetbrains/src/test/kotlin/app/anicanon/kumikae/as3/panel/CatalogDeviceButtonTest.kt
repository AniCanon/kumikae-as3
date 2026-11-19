package app.anicanon.kumikae.as3.panel

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals

class CatalogDeviceButtonTest {
    @Test fun `first binding exposes the device action to keyboard and screen reader users`() {
        val button = CatalogDeviceButton()
        val device = CatalogDevice("Phone", "iOS · Running", running = true, android = false, available = true)
        var calls = 0
        button.activate = { calls++ }
        button.device = device
        assertEquals("Open Phone", button.accessibleContext.accessibleName)
        button.doClick(0)
        button.device = device.copy(available = false, detail = "Shutting down")
        button.doClick(0)
        assertEquals(1, calls)
        assertEquals("Shutting down", button.accessibleContext.accessibleDescription)
    }
}
