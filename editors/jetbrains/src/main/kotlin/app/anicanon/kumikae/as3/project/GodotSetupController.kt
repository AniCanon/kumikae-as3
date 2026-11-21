package app.anicanon.kumikae.as3.project

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.panel.BackgroundTasks
import java.nio.file.Path

internal class GodotSetupController(private val setup: GodotSetup, private val worker: BackgroundTasks) {
    var installation by mutableStateOf<GodotInstallation?>(null)
        private set
    var busy by mutableStateOf(false)
        private set
    var message by mutableStateOf("")
        private set

    fun detect() = run("Looking for Godot 4.7…", setup::detect)
    fun choose(path: Path) = run("Checking the selected Godot…") { setup.choose(path) }
    fun install() = run("Downloading and verifying Godot ${LocalGodotSetup.VERSION}…", setup::install)

    private fun run(status: String, work: () -> GodotInstallation?) {
        if (busy) return
        busy = true
        message = status
        val submitted = worker.submit(work) { result ->
            busy = false
            result.onSuccess {
                installation = it
                message = ""
            }.onFailure {
                message = it.localizedMessage ?: "Godot setup failed. Try again."
            }
        }
        if (!submitted) {
            busy = false
            message = ""
        }
    }
}
