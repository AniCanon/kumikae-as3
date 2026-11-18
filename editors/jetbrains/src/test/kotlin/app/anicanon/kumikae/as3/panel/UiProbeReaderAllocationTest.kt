package app.anicanon.kumikae.as3.panel

import org.junit.Test
import kotlin.test.assertEquals

class UiProbeReaderAllocationTest {
    @Test fun missingReaderReportsUnavailable() {
        assertEquals("unavailable", UiProbeReaderAllocation("missing frame reader").bytes())
    }
}
