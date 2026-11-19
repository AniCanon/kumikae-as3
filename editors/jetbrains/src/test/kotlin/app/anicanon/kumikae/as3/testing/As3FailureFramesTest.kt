package app.anicanon.kumikae.as3.testing

import org.junit.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class As3FailureFramesTest {
    @Test fun compilerAndEngineDiagnosticsExposeOnlyTheirSourceCoordinates() {
        val text = """
            Compiling tests
            tests/Bad Score.as:7:21: error: unknown name
            SCRIPT ERROR: Parse Error: bad type
                      at: GDScript::reload (res://tests/a (1).gd:9)
            GDScript backtrace (most recent call first):
                [0] _ready (res://scenes/main.gd:17)
                at check()[tests/Other.as:3]
                at: draw (servers/rendering.cpp:78)
            tests/Bad.as:0:1: error: no line
        """.trimIndent()
        val frames = As3FailureFrames.read(text)
        assertEquals(
            listOf("tests/Bad Score.as:7:21", "res://tests/a (1).gd:9", "res://scenes/main.gd:17", "tests/Other.as:3"),
            frames.map { text.substring(it.start, it.end) },
        )
        assertEquals(listOf(7, 9, 17, 3), frames.map { it.line })
    }

    @Test fun utf8ByteColumnsMapToUtf16CharacterOffsets() {
        val line = "/* 🚀 */ var n:int = \"invalid\";"
        val quoteCharOffset = line.indexOf("\"invalid\"")
        val quoteByteColumn = line.substring(0, quoteCharOffset).toByteArray(Charsets.UTF_8).size + 1
        assertEquals(quoteCharOffset, As3FailureFrames.utf16Column(line, quoteByteColumn))
        assertEquals(0, As3FailureFrames.utf16Column(line, 1))
        assertEquals(0, As3FailureFrames.utf16Column(line, 0))
        // Mid-emoji byte column snaps back to the start of the emoji.
        assertEquals(3, As3FailureFrames.utf16Column(line, 4))
        assertEquals(3, As3FailureFrames.utf16Column(line, 6))
        // Past-EOL byte column clamps to end of line.
        assertEquals(line.length, As3FailureFrames.utf16Column(line, 500))
        assertEquals(0, As3FailureFrames.utf16Column("", 10))
    }

    @Test fun frameworkPathsRecognizeAbsoluteRelativeAndWindowsLocations() {
        fun frame(path: String) = As3FailureFrame(path, 1, 0, 1, null, null)
        assertTrue(frame("/repo/testing/as3/app/anicanon/kumikae/testing/Assert.as").framework)
        assertTrue(frame("app/anicanon/kumikae/testing/Assert.as").framework)
        assertTrue(frame("""C:\proj\app\anicanon\kumikae\testing\Suite.as""").framework)
        assertFalse(frame("tests/EntityTest.as").framework)
    }

    @Test fun assertionFailurePrefersTestMethodAndClosuresOverSharedHelpers() {
        val helperTrace = """
            Error: Expected true
            	at app.anicanon.kumikae.testing.Assert/assertTrue()[app/anicanon/kumikae/testing/Assert.as:12]
            	at game.EntityTest/expect()[tests/EntityTest.as:132]
            	at game.EntityTest/checksHealth()[tests/EntityTest.as:32]
            	at app.anicanon.kumikae.testing.Suite/run()[app/anicanon/kumikae/testing/Suite.as:40]
        """.trimIndent()
        val ordered = As3FailureFrames.preferredOrder(
            As3FailureFrames.read(helperTrace),
            "game.EntityTest/checksHealth",
        )
        assertEquals(listOf(32, 132), ordered.map { it.line })

        val closureTrace = """
            Error: Expected true
            	at app.anicanon.kumikae.testing.Assert/assertTrue()[/lib/app/anicanon/kumikae/testing/Assert.as:12]
            	at game.EntityTest/expect()[tests/EntityTest.as:132]
            	at game.EntityTest/checksAll/function()[tests/EntityTest.as:45]
            	at game.EntityTest/checksAll()[tests/EntityTest.as:44]
        """.trimIndent()
        val closureOrdered = As3FailureFrames.preferredOrder(
            As3FailureFrames.read(closureTrace),
            "game.EntityTest/checksAll",
        )
        assertEquals(listOf(45, 44, 132), closureOrdered.map { it.line })

        val runtimeTrace = """
            TypeError: null
            	at game.Entity/move()[src/Entity.as:48]
            	at game.EntityTest/checksHealth()[tests/EntityTest.as:32]
        """.trimIndent()
        val runtimeOrdered = As3FailureFrames.preferredOrder(
            As3FailureFrames.read(runtimeTrace),
            "game.EntityTest/checksHealth",
        )
        assertEquals(listOf(48, 32), runtimeOrdered.map { it.line })
    }

    @Test fun rerunCaseIdentityReconstructsHierarchicalAndSyntheticFailures() {
        assertEquals(
            "Game tests::qa.Score::addsPoints",
            As3TestConsole.caseIdentity(listOf("[root]", "AS3 Tests", "Game tests", "qa.Score", "addsPoints")),
        )
        assertEquals(
            "Compile",
            As3TestConsole.caseIdentity(listOf("[root]", "AS3 Tests", "Compile")),
        )
        assertEquals(
            "Build",
            As3TestConsole.caseIdentity(listOf("[root]", "AS3 Tests", "Build", "Build")),
        )
        assertEquals(
            "Game tests",
            As3TestConsole.caseIdentity(listOf("[root]", "AS3 Tests", "Game tests", "qa.Score", "@AfterClass")),
        )
    }
}
