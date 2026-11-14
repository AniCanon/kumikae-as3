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
}
