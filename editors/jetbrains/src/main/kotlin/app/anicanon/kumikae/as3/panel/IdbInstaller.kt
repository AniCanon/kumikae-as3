package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.ScriptFailed
import app.anicanon.kumikae.as3.ShippedScript
import java.nio.file.Path

/** Installs idb on this Mac. It blocks for as long as the install takes and returns what it printed. */
internal fun interface IdbInstaller {
    fun install(): String
}

internal class IdbInstallFailed(message: String, val output: String) : Exception(message)

/** Runs the install script that ships with the plugin: the same one a person can run from `tools/`. */
internal class ScriptIdbInstaller(
    private val script: () -> String = ::shipped,
    private val home: Path = Path.of(System.getProperty("user.home")),
) : IdbInstaller {
    override fun install(): String = try {
        ShippedScript(script, home).run()
    } catch (failure: ScriptFailed) {
        throw IdbInstallFailed(
            "The install ${failure.message?.replaceFirstChar(Char::lowercase)?.removePrefix("it ")}",
            failure.output,
        )
    }

    companion object {
        fun shipped(): String = ShippedScript.read("/install-idb.sh")
    }
}
