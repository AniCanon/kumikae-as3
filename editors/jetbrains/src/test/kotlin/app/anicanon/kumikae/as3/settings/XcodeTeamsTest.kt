package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals

class XcodeTeamsTest {
    @Test fun `account cache yields distinct named teams and identifies personal teams`() {
        val cache = """
            {"first":[{"teamID":"EXAMPLE123","teamName":"Studio"},{"teamID":"incomplete","teamName":"Ignore"}],
             "second":[{"teamID":"PERSONAL12","teamName":"Alex","isFreeProvisioningTeam":true},
                       {"teamID":"EXAMPLE123","teamName":"Studio"},{}]}
        """.trimIndent()
        assertEquals(
            listOf(SigningTeam("PERSONAL12", "Alex", true), SigningTeam("EXAMPLE123", "Studio")),
            XcodeTeams.parse(cache),
        )
    }
}
