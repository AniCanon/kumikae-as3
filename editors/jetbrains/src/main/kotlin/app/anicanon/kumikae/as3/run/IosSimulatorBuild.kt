package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.ScriptFailed
import app.anicanon.kumikae.as3.ShippedScript
import java.nio.file.Files
import java.nio.file.Path

/**
 * Builds the project's game for the iOS Simulator: Godot writes an Xcode project, Xcode builds it. The
 * work is the script `tools/build-ios-simulator.sh`, which the plugin carries and a person can run.
 */
internal class IosSimulatorBuild(
    private val project: Path,
    private val godot: Path,
    private val template: Path,
    private val library: Path?,
    private val script: () -> String = { ShippedScript.read("/build-ios-simulator.sh") },
) {
    /** The built .app. What the build prints is passed on as it comes. */
    fun build(say: (String, Boolean) -> Unit): String {
        val arguments = listOf(project, project.resolve(OUTPUT), godot, template).map(Path::toString) +
            listOfNotNull(library?.toString())
        val printed = try {
            ShippedScript(script, limitMinutes = LIMIT_MINUTES).run(arguments) { say("$it\n", false) }
        } catch (failure: ScriptFailed) {
            // The script's last line is its reason, written for a person.
            error(failure.output.lines().lastOrNull { it.isNotBlank() } ?: "The build failed. ${failure.message}")
        }
        return printed.lines().last { it.isNotBlank() }
    }

    companion object {
        // Inside what Godot keeps for itself: not part of the game, and already out of version control.
        private const val OUTPUT = ".godot/ios-simulator"
        private const val LIMIT_MINUTES = 30L
        private const val TEMPLATE = "ios-with-arm64-simulator.zip"

        /** Whether the project says how it is exported for iOS. */
        fun canBuild(project: Path): Boolean = runCatching {
            Files.readString(project.resolve("export_presets.cfg")).lines().any { it.trim() == "platform=\"iOS\"" }
        }.getOrDefault(false)

        /**
         * The export template with a Simulator library for Apple silicon: the one that was set, the one
         * kept in the home folder, or the one beside a build of the compiler's own repository.
         */
        fun findTemplate(configured: String, home: Path, compiler: Path?): Path? {
            val set = configured.trim().takeIf { it.isNotEmpty() }?.let(Path::of)
            val kept = home.resolve("Library/Application Support/Kumikae AS3/$TEMPLATE")
            val beside = compiler?.parent?.parent?.parent?.resolve("spike/third_party/godot-ios-template/$TEMPLATE")
            return listOfNotNull(set, kept, beside).firstOrNull { Files.isRegularFile(it) }
        }
    }
}
