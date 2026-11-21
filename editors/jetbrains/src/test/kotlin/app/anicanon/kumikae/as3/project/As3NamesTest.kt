package app.anicanon.kumikae.as3.project

import org.junit.jupiter.api.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class As3NamesTest {
    @Test fun `reserved words cannot name a file or package but contextual words can`() {
        assertFalse(As3Names.valid("class"))
        assertTrue(As3Names.valid("native"))
        assertTrue(As3Names.valid("get"))
        assertTrue(As3Names.valid("set"))
    }
}
