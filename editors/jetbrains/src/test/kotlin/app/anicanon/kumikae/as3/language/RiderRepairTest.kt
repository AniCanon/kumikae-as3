package app.anicanon.kumikae.as3.language

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertSame

class RiderRepairTest {
    @Test fun `a failed repair is reported and does not prevent the next repair`() {
        val error = IllegalStateException("Unsupported native signature")
        val failures = mutableListOf<Pair<String, Exception>>()
        var nextRan = false
        val report: (String, Exception) -> Unit = { name, cause -> failures += name to cause }
        runRiderRepair("feedback", report) { throw error }
        runRiderRepair("panels", report) { nextRan = true }
        assertEquals(true, nextRan)
        assertEquals("feedback", failures.single().first)
        assertSame(error, failures.single().second)
    }
}
