package app.anicanon.kumikae.as3.project

import org.junit.jupiter.api.Test
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ProjectTemplateTest {
    private val root = Files.createTempDirectory("template")

    @Test fun `a new game has a project file, a scene, a script and the extension's description`() {
        val files = ProjectTemplate("Star Hopper", emptySet()).files()
        assertEquals(
            setOf("project.godot", "main.tscn", "Main.as", "kas3.gdextension", ".gitignore", "README.md"),
            files.keys,
        )
        assertTrue(files.getValue("project.godot").contains("config/name=\"Star Hopper\""))
        assertTrue(files.getValue("project.godot").contains("run/main_scene=\"res://main.tscn\""))
        assertTrue(files.getValue("main.tscn").contains("path=\"res://Main.as\""))
        assertTrue(files.getValue("Main.as").contains("public class Main extends Node2D"))
        assertTrue(files.getValue("kas3.gdextension").contains("entry_symbol = \"kas3_godot_init\""))
        assertFalse(files.getValue("README.md").contains("iOS"))
    }

    @Test fun `choosing iOS adds its export settings, with a bundle identifier made from the name`() {
        val template = ProjectTemplate("Star Hopper 2!", setOf(Platform.IOS))
        assertEquals("org.example.starhopper2", template.bundle)
        val presets = template.files().getValue("export_presets.cfg")
        assertTrue(presets.contains("platform=\"iOS\""))
        assertTrue(presets.contains("application/bundle_identifier=\"org.example.starhopper2\""))
        assertTrue(presets.contains("include_filter=\"*.as\""))
        assertTrue(template.files().getValue("README.md").contains("## iOS"))
    }

    @Test fun `a name with nothing usable in it still gives a bundle identifier`() {
        assertEquals("org.example.game", ProjectTemplate("!!!", emptySet()).bundle)
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

    @Test fun `what can be chosen says why when it can't`() {
        val without = Platform.choices(consolePlugin = false)
        assertEquals(
            listOf(Platform.DESKTOP, Platform.IOS, Platform.ANDROID, Platform.CONSOLE),
            without.map {
                it.platform
            },
        )
        assertTrue(without.first { it.platform == Platform.DESKTOP }.let { it.available && it.always })
        assertTrue(without.first { it.platform == Platform.IOS }.available)
        assertFalse(without.first { it.platform == Platform.ANDROID }.available)
        assertFalse(without.first { it.platform == Platform.CONSOLE }.available)
        assertTrue(Platform.choices(consolePlugin = true).first { it.platform == Platform.CONSOLE }.available)
    }
}
