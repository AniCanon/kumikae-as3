package app.anicanon.kumikae.as3.run

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class IosDevicesTest {
    @Test fun `physical devices exclude simulators and retain unavailable phones`() {
        val json = """{"result":{"devices":[
          {"identifier":"phone","properties":{"hardware":{"platform":"iOS","deviceType":"iPhone"},
           "state":{"name":"Test phone"},"connection":{"state":"connected"}}},
          {"identifier":"sim","properties":{"hardware":{"platform":"iOS","reality":"simulated"},
           "state":{"name":"Test simulator"},"connection":{"state":"connected"}}},
          {"identifier":"tablet","properties":{"hardware":{"platform":"iOS","deviceType":"iPad"},
           "state":{"name":"Test tablet"},"connection":{"state":"unavailable"}}}
        ]}}"""
        val devices = IosDevices.physical(json)
        assertEquals(listOf("phone", "tablet"), devices.map { it.id })
        assertEquals(listOf(true, false), devices.map { it.available })
    }

    @Test fun `a paired on-demand tunnel is available and builds use the hardware UDID`() {
        val json = """{"result":{"devices":[{"identifier":"core-device",
          "hardwareProperties":{"platform":"iOS","udid":"build-destination"},
          "connectionProperties":{"tunnelState":"disconnected","pairingState":"paired"}}]}}"""
        val device = IosDevices.physical(json).single()
        assertEquals("core-device", device.id)
        assertEquals("build-destination", device.buildId)
        assertEquals(true, device.available)
        assertEquals(false, IosDevices.physical(json.replace("paired", "unpaired")).single().available)
    }

    @Test fun `older devicectl format remains readable`() {
        val json = """{"result":{"devices":[{"identifier":"phone",
          "hardwareProperties":{"platform":"iOS"},"deviceProperties":{"name":"Phone"},
          "connectionProperties":{"tunnelState":"connected"}}]}}"""
        assertEquals("Phone", IosDevices.physical(json).single().name)
    }

    @Test fun `malformed discovery never looks like an empty device list`() {
        assertFailsWith<IllegalArgumentException> { IosDevices.physical("{}") }
    }
}
