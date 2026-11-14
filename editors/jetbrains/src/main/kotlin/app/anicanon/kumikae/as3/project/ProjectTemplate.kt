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
internal class ProjectTemplate(private val name: String, private val platforms: Set<Platform>) {
    /** Made from the name: lower-case letters and digits, starting with a letter. A placeholder to change. */
    val bundle: String = name.lowercase().filter { it in 'a'..'z' || it in '0'..'9' }.let {
        "org.example." + when {
            it.isEmpty() -> "game"
            it.first().isDigit() -> "g$it"
            else -> it
        }
    }

    fun files(): Map<String, String> = buildMap {
        put("project.godot", PROJECT.replace("{name}", name.replace("\"", "'")))
        put("main.tscn", SCENE)
        put("Main.as", SCRIPT)
        put("kas3.gdextension", EXTENSION)
        put(".gitignore", ".godot/\nbin\n")
        put("README.md", readme())
        if (Platform.IOS in platforms) put("export_presets.cfg", IOS_PRESET.replace("{bundle}", bundle))
    }

    /**
     * Writes what is not there yet and returns what it wrote. `library` is the folder that holds the
     * extension's built library: the project's `bin` becomes a link to it, so a rebuild reaches the game.
     */
    fun write(folder: Path, library: Path?): List<String> {
        Files.createDirectories(folder)
        val written = files().filter { (file, text) ->
            val target = folder.resolve(file)
            if (Files.exists(target)) {
                false
            } else {
                Files.writeString(target, text)
                true
            }
        }.keys.toMutableList()
        val bin = folder.resolve("bin")
        if (library != null && !Files.exists(bin) && !Files.isSymbolicLink(bin)) {
            Files.createSymbolicLink(bin, library)
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
            append("\n## iOS\n\n`export_presets.cfg` has an iOS preset. Before the first export, set your team ")
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
        /** The extension's library, where a build of the compiler's repository leaves it beside `kas3c`. */
        fun libraryBeside(compiler: Path?): Path? {
            val folder = compiler?.parent?.parent?.resolve("tests/godot_project/bin") ?: return null
            val built = Files.isDirectory(folder) && Files.list(folder).use { files ->
                files.anyMatch { it.fileName.toString().startsWith("libkas3_godot") }
            }
            return folder.takeIf { built }
        }

        private const val PROJECT = """config_version=5

[application]

config/name="{name}"
run/main_scene="res://main.tscn"
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

        private const val SCENE = """[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://Main.as" id="1"]

[node name="Main" type="Node2D"]
position = Vector2(480, 270)
script = ExtResource("1")
"""

        private const val SCRIPT = """package {
	import godot.*;

	// The game's first scene: a disc that follows the arrow keys.
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

application/app_store_team_id=""
application/bundle_identifier="{bundle}"
application/export_project_only=true
application/short_version="1.0"
application/version="1.0"
architectures/arm64=true
"""
    }
}
