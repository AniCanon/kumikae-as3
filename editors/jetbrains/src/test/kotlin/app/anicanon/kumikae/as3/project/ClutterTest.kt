package app.anicanon.kumikae.as3.project

import org.junit.jupiter.api.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ClutterTest {
    @Test fun `what Godot and the IDE keep for themselves is clutter`() {
        for (folder in listOf(".godot", ".idea", "bin")) assertTrue(Clutter.isClutter(folder, isFolder = true), folder)
        for (file in listOf("Main.as.uid", "icon.png.import", "kas3.gdextension", "kas3.gdextension.uid")) {
            assertTrue(Clutter.isClutter(file, isFolder = false), file)
        }
    }

    @Test fun `what a game is made of is not`() {
        for (folder in listOf(
            "scripts",
            "levels",
            "binaries",
        )) {
            assertFalse(Clutter.isClutter(folder, isFolder = true), folder)
        }
        for (file in listOf(
            "Main.as",
            "main.tscn",
            "project.godot",
            "export_presets.cfg",
            "README.md",
            ".gitignore",
            "bin",
        )) {
            assertFalse(Clutter.isClutter(file, isFolder = false), file)
        }
    }

    @Test fun `build folders at a game's root are kept out of the IDE's index`() {
        // A Simulator build stages a copy of the project in `.build`: indexed, search and navigation offered
        // those copies beside the game's own files, and nothing is completed in a copy.
        val root = "/games/sprint"
        assertTrue("/games/sprint/.build" in Clutter.unindexed(root))
        assertTrue("/games/sprint/build" in Clutter.unindexed(root))
        assertTrue("/games/sprint/bin" in Clutter.unindexed(root))
        // The IDE's own folder is its own business, and Godot's holds what the Godot plugin registers.
        assertFalse("/games/sprint/.idea" in Clutter.unindexed(root))
        assertFalse("/games/sprint/.godot" in Clutter.unindexed(root))
        // Only at the root: a package named build is the game's.
        assertFalse(Clutter.unindexed(root).any { it.startsWith("/games/sprint/src") })
    }

    @Test fun `what Play builds under Godot's folder is kept out of the IDE's index, and nothing else of it`() {
        // Play writes the game as C++, the libraries built from it and their logs under `.godot/kas3`, again
        // at each save: indexed, every save would index generated code nobody opens.
        val unindexed = Clutter.unindexed("/games/sprint")
        assertTrue("/games/sprint/.godot/kas3" in unindexed)
        assertFalse("/games/sprint/.godot" in unindexed)
        assertTrue(unindexed.filter { it.startsWith("/games/sprint/.godot") } == listOf("/games/sprint/.godot/kas3"))
        // The folder is still Godot's and the IDE's to hide in the Game view, whole.
        assertTrue(Clutter.hideFolder(".godot", projectRoot = true))
    }

    @Test fun `source packages named build remain visible`() {
        assertFalse(Clutter.hideFolder("build", projectRoot = false))
        assertTrue(Clutter.hideFolder("build", projectRoot = true))
    }
}
