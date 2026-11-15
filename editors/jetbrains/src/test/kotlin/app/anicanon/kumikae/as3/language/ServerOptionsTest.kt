package app.anicanon.kumikae.as3.language

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals

class ServerOptionsTest {
    @Test fun `code in a hover is marked as this plugin's language, which the IDE knows by that name`() {
        assertEquals(mapOf("codeLanguage" to "KumikaeAS3"), serverOptions())
        assertEquals(As3Language, com.intellij.lang.Language.findLanguageByID(serverOptions().getValue("codeLanguage")))
    }
}
