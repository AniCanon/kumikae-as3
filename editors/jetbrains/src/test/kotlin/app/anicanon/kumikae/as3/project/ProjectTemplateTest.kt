package app.anicanon.kumikae.as3.project

import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ProjectTemplateTest {
    @TempDir lateinit var root: Path

    @Test fun `the disc of a new game goes the way it is swiped, by a finger or the mouse`() {
        val files = ProjectTemplate("Star Hopper", emptySet()).files()
        val script = files.getValue("src/org/example/starhopper/view/Main.as")
        assertTrue(script.contains("override public function _unhandledInput(event:InputEvent):void"))
        assertTrue(script.contains("event as InputEventScreenTouch"))
        // A mouse is turned into touches on a computer, so the same swipe works there.
        assertTrue(files.getValue("project.godot").contains("pointing/emulate_touch_from_mouse=true"))
        assertTrue(files.getValue("src/org/example/starhopper/view/Hello.as").contains("Swipe"))
    }

    @Test fun `a new game has a project file, a scene, a script and the extension's description`() {
        val files = ProjectTemplate("Star Hopper", emptySet()).files()
        assertTrue(files.getValue("project.godot").contains("config/version=\"1.0.0\""))
        assertTrue(
            files.getValue(
                "src/org/example/starhopper/view/Main.as",
            ).startsWith("package org.example.starhopper.view {"),
        )
        assertTrue(
            files.getValue("tests/org/example/starhopper/GameTest.as").contains("app.anicanon.kumikae.testing.Assert"),
        )
        assertEquals(
            setOf(
                "project.godot",
                "scenes/main.tscn",
                "src/org/example/starhopper/view/Main.as",
                "src/org/example/starhopper/view/Hello.as",
                "tests/org/example/starhopper/GameTest.as",
                "kas3-tests.json",
                "assets/icon.svg",
                "kas3.gdextension",
                ".gitignore",
                "README.md",
                ".godot/extension_list.cfg",
            ),
            files.keys,
        )
        assertTrue(files.getValue("project.godot").contains("config/name=\"Star Hopper\""))
        assertTrue(files.getValue("project.godot").contains("run/main_scene=\"res://scenes/main.tscn\""))
        assertTrue(
            files.getValue("scenes/main.tscn").contains("path=\"res://src/org/example/starhopper/view/Main.as\""),
        )
        assertTrue(
            files.getValue("src/org/example/starhopper/view/Main.as").contains("public class Main extends Node2D"),
        )
        assertTrue(files.getValue("kas3.gdextension").contains("entry_symbol = \"kas3_godot_init\""))
        assertTrue(
            files.getValue("scenes/main.tscn").contains("path=\"res://src/org/example/starhopper/view/Hello.as\""),
        )
        assertTrue(
            files.getValue("src/org/example/starhopper/view/Hello.as").contains("text = \"Hello from ActionScript 3"),
        )
        // Godot loads the extension from this list: without it the scripts have no loader until an import.
        assertEquals("res://kas3.gdextension\n", files.getValue(".godot/extension_list.cfg"))
        // Godot refuses to export for a phone without an icon.
        assertTrue(files.getValue("project.godot").contains("config/icon=\"res://assets/icon.svg\""))
        assertTrue(files.getValue("assets/icon.svg").startsWith("<svg"))
        assertFalse(files.getValue("README.md").contains("iOS"))
    }

    @Test fun `choosing iOS adds its export settings, with a bundle identifier made from the name`() {
        val template = ProjectTemplate("Star Hopper 2!", setOf(Platform.IOS))
        assertEquals("org.example.starhopper2", template.bundle)
        val presets = template.files().getValue("export_presets.cfg")
        assertTrue(presets.contains("platform=\"iOS\""))
        assertTrue(presets.contains("application/bundle_identifier=\"org.example.starhopper2\""))
        assertTrue(presets.contains("include_filter=\"*.as\""))
        // Godot will not write the Xcode project without a team: a stand-in is enough for the Simulator.
        assertTrue(presets.contains("application/app_store_team_id=\"\""))
        assertTrue(template.files().getValue("README.md").contains("## iOS"))
    }

    @Test fun `a name with nothing usable in it still gives a bundle identifier`() {
        assertEquals("org.example.game", ProjectTemplate("!!!", emptySet()).bundle)
        assertEquals("org.example.game", ProjectTemplate.defaultBundle("---"))
        assertEquals("org.example.gclass", ProjectTemplate.defaultBundle("Class"))
        assertTrue(ProjectTemplate("Class", emptySet()).files().getValue("kas3-tests.json").contains("src/**/*.as"))
        assertTrue(
            ProjectTemplate("Class", emptySet()).files().getValue("kas3-tests.json").contains("\"runtime\":\"godot\""),
        )
        assertFalse(ProjectTemplate("Class", emptySet()).files().getValue("kas3-tests.json").contains("exclude"))
        assertEquals("org.example.g2048", ProjectTemplate("2048", emptySet()).bundle)
    }

    @Test fun `the files are written, and nothing that is there already is replaced`() {
        val folder = root.resolve("game")
        Files.createDirectories(folder)
        Files.writeString(folder.resolve("README.md"), "mine")
        val written = ProjectTemplate("Game", emptySet()).write(folder, library = null)
        assertEquals("mine", Files.readString(folder.resolve("README.md")))
        assertFalse("README.md" in written)
        assertTrue(Files.isRegularFile(folder.resolve("project.godot")) && "project.godot" in written)
        assertFalse(Files.exists(folder.resolve("bin")))
    }

    @Test fun `the extension's library is linked in from where it was built`() {
        val built = Files.createDirectories(root.resolve("sdk/compiler/tests/godot_project/bin"))
        Files.createFile(built.resolve("libkas3_godot.dylib"))
        val compiler = Files.createFile(Files.createDirectories(root.resolve("sdk/compiler/build")).resolve("kas3c"))
        assertEquals(built, ProjectTemplate.libraryBeside(compiler))
        assertNull(ProjectTemplate.libraryBeside(Path.of("/usr/local/bin/kas3c")))
        assertNull(ProjectTemplate.libraryBeside(null))
        val folder = root.resolve("linked")
        ProjectTemplate("Game", emptySet()).write(folder, library = built)
        assertTrue(Files.isSymbolicLink(folder.resolve("bin")))
        assertTrue(Files.isRegularFile(folder.resolve("bin/libkas3_godot.dylib")))
    }

    @Test fun `a new Web game includes its selected preset and bundled WebAssembly runtime`() {
        val kit = Files.createDirectories(root.resolve("kit/web"))
        Files.writeString(kit.resolve("libkas3_godot.wasm"), "web-runtime")
        val game = root.resolve("game")
        ProjectTemplate("Game", setOf(Platform.WEB)).write(game, kit.parent, link = false)
        assertEquals("web-runtime", Files.readString(game.resolve("bin/web/libkas3_godot.wasm")))
        assertTrue(Files.readString(game.resolve("kas3.gdextension")).contains("res://bin/web/libkas3_godot.wasm"))
        assertTrue(Files.readString(game.resolve("export_presets.cfg")).contains("variant/extensions_support=true"))
    }

    @Test fun `what can be chosen says why when it can't`() {
        val choices = Platform.choices()
        assertEquals(
            listOf(Platform.DESKTOP, Platform.IOS, Platform.ANDROID, Platform.WEB),
            choices.map {
                it.platform
            },
        )
        assertTrue(choices.first().always)
        val game = ProjectTemplate("Game", setOf(Platform.ANDROID, Platform.IOS))
        val config = game.files().getValue("export_presets.cfg")
        assertTrue(config.contains("package/unique_name="))
        assertTrue(config.contains("application/bundle_identifier="))
    }

    @Test fun `a package name that is given is the app's identifier, and its shape is checked`() {
        val template = ProjectTemplate("Star Hopper", setOf(Platform.IOS), bundle = " com.studio.star-hopper ")
        assertEquals("com.studio.star-hopper", template.bundle)
        assertTrue(template.files().getValue("export_presets.cfg").contains("\"com.studio.star-hopper\""))
        assertTrue(ProjectTemplate.isBundle("com.studio.star-hopper") && ProjectTemplate.isBundle("org.example.g2048"))
        for (wrong in listOf("", "game", "com..game", ".com.game", "com.game.", "com.my game", "1com.game")) {
            assertFalse(ProjectTemplate.isBundle(wrong), wrong)
        }
    }

    @Test fun `the library the plugin carries is copied in, not linked`() {
        val carried = Files.createDirectories(root.resolve("plugin/native"))
        Files.writeString(carried.resolve("libkas3_godot.dylib"), "library")
        Files.writeString(carried.resolve("kas3c"), "compiler")
        val folder = root.resolve("copied")
        ProjectTemplate("Game", emptySet()).write(folder, library = carried, link = false)
        assertFalse(Files.isSymbolicLink(folder.resolve("bin")))
        assertEquals("library", Files.readString(folder.resolve("bin/libkas3_godot.dylib")))
        assertFalse(Files.exists(folder.resolve("bin/kas3c")))
        assertTrue(Files.isRegularFile(folder.resolve(".godot/extension_list.cfg")))
    }
}
