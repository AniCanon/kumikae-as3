package app.anicanon.kumikae.as3.run

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class AndroidDestinationTest {
    @Test fun `the toolbar offers only connected devices that can load the packaged runtime`() {
        val connected = "emulator-5570 device product:sdk model:Pixel_9 transport_id:1"
        assertNull(androidDestination(connected) { "x86_64,x86" })
        assertNull(androidDestination("emulator-5570 offline") { error("Offline device queried") })
        val device = androidDestination(connected) { "arm64-v8a,armeabi-v7a" }
        assertEquals("Pixel 9", device?.model)
    }
}
