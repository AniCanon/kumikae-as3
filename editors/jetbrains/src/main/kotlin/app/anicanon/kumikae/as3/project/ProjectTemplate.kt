package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.GodotConfig
import java.nio.file.Files
import java.nio.file.Path

/** Where a game can be made to run. */
internal enum class Platform(val label: String) {
    DESKTOP("Desktop (this computer)"),
    IOS("iOS"),
    ANDROID("Android"),
    ;

    /** One line of the wizard: whether the platform can be chosen, and if not, why. */
    data class Choice(val platform: Platform, val available: Boolean, val always: Boolean, val note: String)

    companion object {
        fun choices(): List<Choice> = listOf(
            Choice(DESKTOP, available = true, always = true, note = "Runs with Godot from the Run button."),
            Choice(
                IOS,
                available = true,
                always = false,
                note = "Adds iOS export settings and an iOS Simulator run target.",
            ),
            Choice(
                ANDROID,
                available = true,
                always = false,
                note = "Adds Android export settings and an Android run target.",
            ),
        )
    }
}

/** The files of a new AS3 game for Godot. Nothing here touches the IDE. */
internal class ProjectTemplate(
    private val name: String,
    private val platforms: Set<Platform>,
    bundle: String = "",
    private val version: String = DEFAULT_VERSION,
) {
    /** The app's identifier on a phone or a console: the one given, or one made from the name to be changed. */
    val bundle: String = bundle.trim().ifEmpty { defaultBundle(name) }

    fun files(): Map<String, String> = buildMap {
        val source = "src/" + bundle.replace('.', '/').replace('-', '_') + "/view"
        val namespace = bundle.replace('-', '_')
        require(namespace.split('.').all(As3Names::valid)) { "Choose a valid ActionScript package name." }
        require(VERSION_PATTERN.matches(version)) { "Enter a version such as 1.0.0." }
        val testRoot = "tests/" + namespace.replace('.', '/')
        put(
            "project.godot",
            PROJECT.replace(
                "{version}",
                version,
            ).replace(
                "{package}",
                bundle,
            ).replace("{name}", GodotConfig.quote(name).removeSurrounding("\"")),
        )
        put("scenes/main.tscn", SCENE.replace("{source}", source))
        put("$source/Main.as", SCRIPT.replace("package {", "package $namespace.view {"))
        put("$source/Hello.as", HELLO.replace("package {", "package $namespace.view {"))
        put(
            "kas3-tests.json",
            """{"version":1,"tests":[{"name":"Game tests","discover":"tests",""" +
                """"sources":["src/**/*.as","tests/**/*.as"],""" +
                """"exclude":["src/**/view/**/*.as"]}]}""" + "\n",
        )
        put(
            "$testRoot/GameTest.as",
            """package $namespace {
    import app.anicanon.kumikae.testing.Assert;

    public class GameTest {
        [Test]
        public function startingScore():void {
            var score:int = 0;
            Assert.assertEquals(0, score);
        }
    }
}
""",
        )
        // Godot will not export for a phone without an icon.
        put("assets/icon.svg", icon())
        // What Godot's editor writes on its first import: with it, the game runs before the editor ever opens.
        put(".godot/extension_list.cfg", "res://kas3.gdextension\n")
        put("kas3.gdextension", EXTENSION)
        put(".gitignore", ".godot/\nbin\n")
        put("README.md", readme())
        var exports = ""
        for (platform in platforms.filter { it == Platform.IOS || it == Platform.ANDROID }) {
            exports = GameTargets.add(exports, platform.label, platform.label, bundle)
        }
        if (exports.isNotEmpty()) put("export_presets.cfg", exports)
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
                val ios = library.resolve("ios")
                if (Files.isDirectory(ios)) {
                    Files.walk(ios).use { entries ->
                        entries.forEach { source ->
                            val target = bin.resolve("ios").resolve(ios.relativize(source))
                            if (Files.isDirectory(source)) {
                                Files.createDirectories(target)
                            } else {
                                Files.createDirectories(target.parent)
                                Files.copy(source, target)
                            }
                        }
                    }
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
            append("\n## iOS\n\nSelect the iOS Simulator run configuration and choose a device. ")
            append("Run builds the export preset and launches its bundle identifier.\n")
        }
    }

    companion object {
        const val DEFAULT_VERSION = "1.0.0"
        val VERSION_PATTERN = Regex("""\d+\.\d+\.\d+""")
        private const val LIBRARY = "libkas3_godot"

        /** The project's icon: the Kumikae mark, to be replaced by the game's own. */
        fun icon(): String = checkNotNull(ProjectTemplate::class.java.getResourceAsStream("/projectIcon.svg")) {
            "The plugin was built without its project icon."
        }.bufferedReader().use { it.readText() }

        /** Made from the name: lower-case letters and digits, starting with a letter. */
        fun defaultBundle(name: String): String = name.lowercase().filter { it in 'a'..'z' || it in '0'..'9' }.let {
            "org.example." + when {
                it.isEmpty() -> "game"
                !As3Names.valid(it) -> "g$it"
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
config/version="{version}"
config/kumikae_package="{package}"
run/main_scene="res://scenes/main.tscn"
config/icon="res://assets/icon.svg"
config/features=PackedStringArray("4.7")

[display]

window/size/viewport_width=960
window/size/viewport_height=540
window/stretch/mode="canvas_items"

[input_devices]

pointing/emulate_touch_from_mouse=true

[rendering]

renderer/rendering_method="gl_compatibility"
renderer/rendering_method.mobile="gl_compatibility"
textures/vram_compression/import_etc2_astc=true
"""

        private const val SCENE = """[gd_scene load_steps=3 format=3]

[ext_resource type="Script" path="res://{source}/Main.as" id="1"]
[ext_resource type="Script" path="res://{source}/Hello.as" id="2"]

[node name="Main" type="Node2D"]

[node name="Background" type="ColorRect" parent="."]
offset_right = 960.0
offset_bottom = 540.0
color = Color(0.09, 0.1, 0.14, 1)
mouse_filter = 2

[node name="Hello" type="Label" parent="."]
offset_left = 24.0
offset_top = 16.0
offset_right = 936.0
offset_bottom = 60.0
theme_override_font_sizes/font_size = 28
mouse_filter = 2
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
			text = "Hello from ActionScript 3. Swipe to send the disc, tap to stop it, or use the arrow keys.";
		}
	}
}
"""

        private const val SCRIPT = """package {
	import godot.*;

	// A disc that goes the way it is swiped, stops when tapped, and follows the arrow keys.
	public class Main extends Node2D {
		private var heading:Vector2 = Vector2.ZERO;
		private var from:Vector2 = Vector2.ZERO;
		private var touching:Boolean = false;

		// A finger on the screen (or the mouse, on a computer) arrives here as a touch, once the game's
		// buttons and other controls have had the chance to take it.
		override public function _unhandledInput(event:InputEvent):void {
			var touch:InputEventScreenTouch = event as InputEventScreenTouch;
			if (touch == null || touch.index != 0) {
				return; // Only the first finger steers.
			}
			if (touch.pressed) {
				from = touch.position;
				touching = true;
			} else if (touching) {
				touching = false;
				if (from.distanceTo(touch.position) > 40) {
					heading = (touch.position - from).normalized(); // A swipe.
				} else {
					heading = Vector2.ZERO; // A tap.
				}
			}
		}

		override public function _process(delta:Number):void {
			var keys:Vector2 = Input.getVector("ui_left", "ui_right", "ui_up", "ui_down");
			// Change the speed while the game runs, and save: it takes effect at once.
			position += (keys == Vector2.ZERO ? heading : keys) * 300 * delta;
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
    }
}
