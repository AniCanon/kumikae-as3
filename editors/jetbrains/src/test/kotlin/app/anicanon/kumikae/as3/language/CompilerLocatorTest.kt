package app.anicanon.kumikae.as3.language

import org.junit.jupiter.api.Test
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions
import kotlin.test.assertEquals
import kotlin.test.assertNull

class CompilerLocatorTest {
    private val root = Files.createTempDirectory("locator")

    private fun executable(path: Path): Path {
        Files.createDirectories(path.parent)
        return Files.createFile(
            path,
            PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")),
        )
    }

    private fun find(
        project: Path,
        configured: String = "",
        searchPath: String = "",
        home: Path = root.resolve("home"),
    ) = CompilerLocator.find(project, configured, searchPath, home)

    @Test fun `nothing is found where there is no compiler`() {
        assertNull(find(Files.createDirectories(root.resolve("game"))))
    }

    @Test fun `the path the person set comes first`() {
        val set = executable(root.resolve("tools/kas3c"))
        executable(root.resolve("bin/kas3c"))
        assertEquals(set, find(root, configured = set.toString(), searchPath = root.resolve("bin").toString()))
    }

    @Test fun `a path that is set but is not there is not used`() {
        val onPath = executable(root.resolve("bin/kas3c"))
        assertEquals(onPath, find(root, configured = "/nowhere/kas3c", searchPath = root.resolve("bin").toString()))
    }

    @Test fun `a build of the compiler in a folder above the project is found`() {
        val built = executable(root.resolve("repo/compiler/build/kas3c"))
        val project = Files.createDirectories(root.resolve("repo/compiler/tests/godot_project"))
        assertEquals(built, find(project))
    }

    @Test fun `the search path and the home folder are looked in last`() {
        val project = Files.createDirectories(root.resolve("game2"))
        val local = executable(root.resolve("home/.local/bin/kas3c"))
        assertEquals(local, find(project))
        val onPath = executable(root.resolve("other/kas3c"))
        assertEquals(onPath, find(project, searchPath = "relative:${root.resolve("other")}"))
    }

    @Test fun `on Windows the program is kas3c dot exe and the search path is split at semicolons`() {
        val project = Files.createDirectories(root.resolve("game4"))
        executable(root.resolve("win/kas3c"))
        val windows = executable(root.resolve("win/kas3c.exe"))
        val searchPath = "relative;${root.resolve("win")}"
        assertEquals(windows, CompilerLocator.find(project, "", searchPath, root.resolve("home"), windows = true))
        val built = executable(root.resolve("game4/compiler/build/kas3c.exe"))
        assertEquals(built, CompilerLocator.find(project, "", searchPath, root.resolve("home"), windows = true))
        // Elsewhere `;` is part of a folder's name, and `kas3c.exe` is not the program.
        val elsewhere = Files.createDirectories(root.resolve("game5"))
        assertNull(CompilerLocator.find(elsewhere, "", searchPath, root.resolve("home"), windows = false))
    }

    @Test fun `the compiler the plugin carries is used when the project has none of its own`() {
        val project = Files.createDirectories(root.resolve("game3"))
        val carried = executable(root.resolve("plugin/native/kas3c"))
        executable(root.resolve("elsewhere/kas3c"))
        val bundled = root.resolve("plugin/native")
        assertEquals(
            carried,
            CompilerLocator.find(project, "", root.resolve("elsewhere").toString(), root.resolve("home"), bundled),
        )
        val own = executable(root.resolve("game3/compiler/build/kas3c"))
        assertEquals(own, CompilerLocator.find(project, "", "", root.resolve("home"), bundled))
    }
}
