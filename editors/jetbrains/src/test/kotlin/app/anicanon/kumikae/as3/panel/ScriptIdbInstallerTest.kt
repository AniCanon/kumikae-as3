package app.anicanon.kumikae.as3.panel

import org.junit.jupiter.api.Test
import java.nio.file.Files
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class ScriptIdbInstallerTest {
    private val home = Files.createTempDirectory("installer-home")

    @Test fun `the script's output, from both streams, is what the install returns`() {
        val output = ScriptIdbInstaller({ "echo one\necho two >&2\n" }, home).install()
        assertEquals(setOf("one", "two"), output.lines().filter { it.isNotBlank() }.toSet())
    }

    @Test fun `a script that fails is a failure that carries what it said`() {
        val failure = assertFailsWith<IdbInstallFailed> {
            ScriptIdbInstaller({ "echo Homebrew is needed\nexit 3\n" }, home).install()
        }
        assertTrue(failure.message!!.contains("exit 3") || failure.message!!.contains("status 3"), failure.message)
        assertTrue(failure.output.contains("Homebrew is needed"))
    }

    @Test fun `the script finds tools an IDE started from the Dock would not`() {
        val path = ScriptIdbInstaller({ "echo \"\$PATH\"\n" }, home).install().trim()
        assertTrue(path.startsWith("/opt/homebrew/bin:/usr/local/bin:$home/.local/bin:"), path)
    }

    @Test fun `nothing of the script is left behind`() {
        val before = Files.list(home).use { it.count() }
        ScriptIdbInstaller({ "true\n" }, home).install()
        assertEquals(before, Files.list(home).use { it.count() })
    }

    @Test fun `the script that ships with the plugin is the one in tools`() {
        val shipped = ScriptIdbInstaller.shipped()
        assertTrue(shipped.startsWith("#!/bin/sh"))
        assertTrue(shipped.contains("brew install facebook/fb/idb-companion") && shipped.contains("fb-idb"))
    }
}
