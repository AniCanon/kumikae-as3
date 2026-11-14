package app.anicanon.kumikae.as3.project

import java.nio.file.Files
import java.nio.file.Path

/** Where a game can be made to run. */
internal enum class Platform(val label: String) {
    DESKTOP("Desktop (this computer)"),
    IOS("iOS"),
    ANDROID("Android"),
    CONSOLE("Nintendo Switch"),
    ;

    /** One line of the wizard: whether the platform can be chosen, and if not, why. */
    data class Choice(val platform: Platform, val available: Boolean, val always: Boolean, val note: String)

    companion object {
        private const val CONSOLE_READY = "Adds a run target from Kumikae for Rider."

        fun choices(consolePlugin: Boolean): List<Choice> = listOf(
            Choice(DESKTOP, available = true, always = true, note = "Runs with Godot from the Run button."),
            Choice(
                IOS,
                available = true,
                always = false,
                note = "Adds iOS export settings and an iOS Simulator run target.",
            ),
            Choice(
                ANDROID,
                available = false,
                always = false,
                note = "Not available yet: the AS3 extension has no Android build.",
            ),
            Choice(
                CONSOLE,
                available = consolePlugin,
                always = false,
                note = if (consolePlugin) CONSOLE_READY else "Needs the Kumikae for Rider plugin.",
            ),
        )
    }
}

/** The files of a new AS3 game for Godot. Nothing here touches the IDE. */
internal class ProjectTemplate(private val name: String, private val platforms: Set<Platform>, bundle: String = "") {
    /** The app's identifier on a phone or a console: the one given, or one made from the name to be changed. */
    val bundle: String = bundle.trim().ifEmpty { defaultBundle(name) }

    fun files(): Map<String, String> = buildMap {
        put("project.godot", PROJECT.replace("{name}", name.replace("\"", "'")))
        put("main.tscn", SCENE)
        put("Main.as", SCRIPT)
        put("Hello.as", HELLO)
        // Godot will not export for a phone without an icon.
        put("icon.svg", icon())
        // What Godot's editor writes on its first import: with it, the game runs before the editor ever opens.
        put(".godot/extension_list.cfg", "res://kas3.gdextension\n")
        put("kas3.gdextension", EXTENSION)
        put(".gitignore", ".godot/\nbin\n")
        put("README.md", readme())
        if (Platform.IOS in platforms) put("export_presets.cfg", IOS_PRESET.replace("{bundle}", bundle))
    }

    /**
     * Writes what is not there yet and returns what it wrote. `library` is the folder that holds the
     * extension's built library. From a build of the compiler's own repository it is linked, so that a
     * rebuild reaches the game; from the plugin it is copied, because the plugin's folder is replaced
     * when the plugin is updated.
     */
    fun write(folder: Path, library: Path?, link: Boolean = true): List<String> {
        Files.createDirectories(folder)
        val written = files().filter { (file, text) ->
            val target = folder.resolve(file)
            if (Files.exists(target)) {
                false
            } else {
                Files.createDirectories(target.parent)
                Files.writeString(target, text)
                true
            }
        }.keys.toMutableList()
        val bin = folder.resolve("bin")
        if (library != null && !Files.exists(bin) && !Files.isSymbolicLink(bin)) {
            if (link) {
                Files.createSymbolicLink(bin, library)
            } else {
                Files.createDirectories(bin)
                Files.list(library).use { files ->
                    files.filter {
                        it.fileName.toString().startsWith(LIBRARY)
                    }.forEach { Files.copy(it, bin.resolve(it.fileName)) }
                }
            }
            written += "bin"
        }
        return written
    }

    private fun readme(): String = buildString {
        append("# $name\n\nA game written in ActionScript 3 for Godot.\n\n")
        append("## Run it\n\nPress Run in the IDE: the \"Game\" configuration starts Godot on this folder. ")
        append("Edit a script while the game runs and save: the game takes the new code and keeps its state.\n\n")
        append("`bin` must hold the AS3 extension's library (`libkas3_godot`). ")
        append("The wizard links it to where the extension was built, when it finds it.\n")
        if (Platform.IOS in platforms) {
            append(
                "\n## iOS\n\n`export_presets.cfg` has an iOS preset. The team id in it is a stand-in, enough for the Simulator: set your own ",
            )
            append("and change the bundle identifier (`$bundle` is a placeholder). The \"iOS Simulator\" run ")
            append("configuration launches that bundle on the simulator shown in the iOS Simulator panel; ")
            append("the app has to be exported with Godot and installed first.\n")
        }
        if (Platform.CONSOLE in platforms) {
            append("\n## Nintendo Switch\n\nBuilding and running there is done by the Kumikae for Rider plugin: ")
            append("see its own run configuration.\n")
        }
    }

    companion object {
        private const val LIBRARY = "libkas3_godot"

        /** The project's icon: the Kumikae mark, to be replaced by the game's own. */
        fun icon(): String = checkNotNull(ProjectTemplate::class.java.getResourceAsStream("/projectIcon.svg")) {
            "The plugin was built without its project icon."
        }.bufferedReader().use { it.readText() }

        /** Made from the name: lower-case letters and digits, starting with a letter. */
        fun defaultBundle(name: String): String = name.lowercase().filter { it in 'a'..'z' || it in '0'..'9' }.let {
            "org.example." + when {
                it.isEmpty() -> "game"
                it.first().isDigit() -> "g$it"
                else -> it
            }
        }

        /** Whether it reads as an identifier: words of letters, digits and hyphens joined by dots, at least two. */
        fun isBundle(text: String): Boolean = Regex("[A-Za-z][A-Za-z0-9-]*(\\.[A-Za-z0-9][A-Za-z0-9-]*)+").matches(text)

        /** Whether the folder holds the extension's library. */
        fun hasLibrary(folder: Path?): Boolean = folder != null && Files.isDirectory(folder) &&
            Files.list(folder).use { files -> files.anyMatch { it.fileName.toString().startsWith(LIBRARY) } }

        /** The extension's library, where a build of the compiler's repository leaves it beside `kas3c`. */
        fun libraryBeside(compiler: Path?): Path? =
            compiler?.parent?.parent?.resolve("tests/godot_project/bin")?.takeIf(::hasLibrary)

        private const val PROJECT = """config_version=5

[application]

config/name="{name}"
run/main_scene="res://main.tscn"
config/icon="res://icon.svg"
config/features=PackedStringArray("4.7")

[display]

window/size/viewport_width=960
window/size/viewport_height=540
window/stretch/mode="canvas_items"

[rendering]

renderer/rendering_method="gl_compatibility"
renderer/rendering_method.mobile="gl_compatibility"
textures/vram_compression/import_etc2_astc=true
"""

        private const val SCENE = """[gd_scene load_steps=3 format=3]

[ext_resource type="Script" path="res://Main.as" id="1"]
[ext_resource type="Script" path="res://Hello.as" id="2"]

[node name="Main" type="Node2D"]

[node name="Background" type="ColorRect" parent="."]
offset_right = 960.0
offset_bottom = 540.0
color = Color(0.09, 0.1, 0.14, 1)

[node name="Hello" type="Label" parent="."]
offset_left = 24.0
offset_top = 16.0
offset_right = 936.0
offset_bottom = 60.0
theme_override_font_sizes/font_size = 28
script = ExtResource("2")

[node name="Disc" type="Node2D" parent="."]
position = Vector2(480, 270)
script = ExtResource("1")
"""

        private const val HELLO = """package {
	import godot.*;

	// The words at the top of the screen.
	public class Hello extends Label {
		override public function _ready():void {
			text = "Hello from ActionScript 3. Move the disc with the arrow keys.";
		}
	}
}
"""

        private const val SCRIPT = """package {
	import godot.*;

	// A disc that follows the arrow keys.
	public class Main extends Node2D {
		override public function _process(delta:Number):void {
			// Change the speed while the game runs, and save: it takes effect at once.
			position += Input.getVector("ui_left", "ui_right", "ui_up", "ui_down") * 300 * delta;
			queueRedraw();
		}

		override public function _draw():void {
			drawCircle(Vector2.ZERO, 24, new Color(0.35, 0.75, 1.0));
		}
	}
}
"""

        private const val EXTENSION = """[configuration]

entry_symbol = "kas3_godot_init"
compatibility_minimum = "4.7"

[libraries]

macos = "res://bin/libkas3_godot.dylib"
linux = "res://bin/libkas3_godot.so"
ios = "res://bin/ios/kas3_godot.xcframework"
"""

        private const val IOS_PRESET = """[preset.0]

name="iOS"
platform="iOS"
runnable=true
advanced_options=false
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter="*.as"
exclude_filter=""
export_path=""
script_export_mode=0

[preset.0.options]

application/app_store_team_id="ABCDE12345"
application/bundle_identifier="{bundle}"
application/export_project_only=true
application/short_version="1.0"
application/version="1.0"
architectures/arm64=true
"""
    }
}
