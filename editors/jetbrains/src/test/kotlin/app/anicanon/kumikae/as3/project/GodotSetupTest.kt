package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.panel.ManualBackgroundTasks
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.condition.EnabledIfEnvironmentVariable
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class GodotSetupTest {
    @TempDir lateinit var root: Path

    @Test fun `discovery skips an incompatible installation and accepts a selected app bundle`() {
        val old = executable("old", "4.6.3.stable.official")
        val app = root.resolve("My Godot.app")
        val current = executable("My Godot.app/Contents/MacOS/Godot", "4.7.stable.official")
        val setup = LocalGodotSetup({ listOf(old, current) }, root)
        assertEquals(GodotInstallation(current, "4.7"), setup.detect())
        assertEquals(setup.detect(), setup.choose(app))
        assertFailsWith<IllegalArgumentException> { setup.choose(old) }
    }

    @Test fun `no engine is available when only a preview is installed`() {
        val preview = executable("preview", "4.7.3.rc1.official")
        assertNull(LocalGodotSetup({ listOf(preview) }, root).detect())
    }

    @Test fun `a corrupt download leaves the install directory ready for a retry`() {
        val setup = LocalGodotSetup({ emptyList() }, root) { Files.writeString(it, "incomplete") }
        assertFailsWith<IllegalStateException> { setup.install() }
        assertEquals(emptyList(), Files.list(root).use { it.toList() })
    }

    @Test
    @EnabledIfEnvironmentVariable(named = "KUMIKAE_GODOT_ARCHIVE", matches = ".+")
    fun `install the official cached archive and reuse it on a second request`() {
        var downloads = 0
        val setup = LocalGodotSetup({ listOf(root.resolve("Godot.app")) }, root) {
            downloads++
            Files.copy(Path.of(System.getenv("KUMIKAE_GODOT_ARCHIVE")), it)
        }
        val engine = setup.install()
        assertEquals("4.7.2", engine.version)
        assertEquals(engine, setup.detect())
        assertEquals(engine, setup.install())
        assertEquals(1, downloads)
    }

    @Test fun `an invalid selection keeps the previous engine and a subsequent selection clears the error`() {
        val app = executable("engine", "4.7.2.stable.official")
        val tasks = ManualBackgroundTasks()
        val controller = GodotSetupController(LocalGodotSetup({ listOf(app) }, root), tasks)
        controller.detect()
        assertTrue(controller.busy)
        controller.install()
        tasks.finish()
        val detected = GodotInstallation(app, "4.7.2")
        assertEquals(detected, controller.installation)
        controller.choose(root.resolve("absent"))
        tasks.finish()
        assertEquals(detected, controller.installation)
        assertTrue(controller.message.contains("Choose a stable Godot"))
        controller.choose(app)
        tasks.finish()
        assertFalse(controller.busy)
        assertEquals("", controller.message)
    }

    private fun executable(name: String, version: String): Path {
        val file = root.resolve(name)
        Files.createDirectories(file.parent)
        Files.writeString(file, "#!/bin/sh\necho '$version'\n")
        check(file.toFile().setExecutable(true))
        return file
    }
}
