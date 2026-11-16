package app.anicanon.kumikae.as3.language

import org.junit.jupiter.api.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class RiderFrameCleanupTest {
    @Test fun `cleanup waits for both project and native helper disposal`() {
        assertFalse(canReleaseClosedFrame(false, true, false, false))
        assertFalse(canReleaseClosedFrame(true, false, false, false))
    }

    @Test fun `installed displayable frame remains untouched`() {
        assertFalse(canReleaseClosedFrame(true, true, true, true))
    }

    @Test fun `disposed helper permits clearing detached root or closed window`() {
        assertTrue(canReleaseClosedFrame(true, true, false, true))
        assertTrue(canReleaseClosedFrame(true, true, true, false))
    }
}
