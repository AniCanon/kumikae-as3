package app.anicanon.kumikae.as3.run

import org.junit.jupiter.api.Test
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class GodotLaunchTest {
    private val root = Files.createTempDirectory("godot-launch")

    private fun executable(path: Path): Path {
        Files.createDirectories(path.parent)
        return Files.createFile(
            path,
            PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")),
        )
    }

    private fun find(configured: String, searchPath: String, applications: Path) =
        GodotLaunch.findGodot(configured, searchPath, applications, installed = emptyList())

    @Test fun `the Godot that was set comes first, then the search path, then the usual application`() {
        val applications = root.resolve("Applications")
        assertNull(find("", "", applications))
        val app = executable(applications.resolve("Godot.app/Contents/MacOS/Godot"))
        assertEquals(app, find("", "", applications))
        val onPath = executable(root.resolve("bin/godot"))
        assertEquals(onPath, find("", "relative:${root.resolve("bin")}", applications))
        val set = executable(root.resolve("tools/godot-4.7"))
        assertEquals(set, find(" $set ", root.resolve("bin").toString(), applications))
        assertEquals(onPath, find("/nowhere/godot", root.resolve("bin").toString(), applications))
    }

    @Test fun `the game is run from its folder, with a scene only when one is named`() {
        val godot = Path.of("/tools/godot")
        val project = Path.of("/games/coin dash")
        assertEquals(listOf("/tools/godot", "--path", "/games/coin dash"), GodotLaunch.command(godot, project, ""))
        assertEquals(
            listOf("/tools/godot", "--path", "/games/coin dash", "res://levels/two.tscn"),
            GodotLaunch.command(godot, project, " res://levels/two.tscn "),
        )
    }

    @Test fun `a project is an AS3 game when it has a Godot project file and a script`() {
        val game = Files.createDirectories(root.resolve("game"))
        assertFalse(GodotLaunch.isAs3Game(game))
        Files.createFile(game.resolve("project.godot"))
        assertFalse(GodotLaunch.isAs3Game(game))
        Files.createFile(Files.createDirectories(game.resolve("scripts/actors")).resolve("Player.as"))
        assertTrue(GodotLaunch.isAs3Game(game))
        // What Godot itself keeps, and hidden folders, are not looked into.
        val other = Files.createDirectories(root.resolve("other"))
        Files.createFile(other.resolve("project.godot"))
        Files.createFile(Files.createDirectories(other.resolve(".godot/cache")).resolve("Old.as"))
        assertFalse(GodotLaunch.isAs3Game(other))
    }
}
