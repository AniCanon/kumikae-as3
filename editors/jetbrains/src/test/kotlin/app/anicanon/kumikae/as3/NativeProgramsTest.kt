package app.anicanon.kumikae.as3

import org.junit.jupiter.api.Test
import java.nio.file.Files
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class NativeProgramsTest {
    private val folder = Files.createTempDirectory("native")

    @Test fun `programs installed without their execute bit can be run`() {
        val compiler = Files.createFile(folder.resolve("kas3c"))
        val bridge = Files.createFile(folder.resolve("simulator-bridge"))
        NativePrograms.makeRunnable(folder)
        assertTrue(Files.isExecutable(compiler))
        assertTrue(Files.isExecutable(bridge))
    }

    @Test fun `other files are left as they are`() {
        val library = Files.createFile(folder.resolve("libkas3_godot.dylib"))
        NativePrograms.makeRunnable(folder)
        assertFalse(Files.isExecutable(library))
    }

    @Test fun `a missing program is no error`() {
        NativePrograms.makeRunnable(folder)
    }
}
