package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.panel.SimulatorRepository

/**
 * One run of an app on the Simulator: choose the device, install if an app was given, launch, and pass
 * on what the app prints. No IDE class is used here; every call may block.
 */
internal class SimulatorRun(
    private val repository: SimulatorRepository,
    bundle: String,
    appPath: String,
    private val output: (text: String, error: Boolean) -> Unit,
) {
    private val bundle = bundle.trim()
    private val appPath = appPath.trim()
    private var device: String? = null
    private var cursor = 0L

    fun start() {
        check(bundle.isNotEmpty()) { "Set the app's bundle identifier in the run configuration." }
        repository.connect()
        val chosen = repository.status().device?.takeIf { it.state == BOOTED }
            ?: repository.devices().firstOrNull { it.state == BOOTED }?.also { repository.select(it.id) }
            ?: error("No simulator is running. Boot one in the iOS Simulator panel, then run again.")
        device = chosen.id
        if (appPath.isNotEmpty()) repository.install(chosen.id, appPath)
        // Output from before this launch belongs to another run.
        cursor = repository.consolePage(0).nextCursor
        repository.launch(chosen.id, bundle)
    }

    /** Reads what was printed since the last call. Returns the app's exit code once it has ended. */
    fun pump(): Int? {
        val page = repository.consolePage(cursor)
        cursor = page.nextCursor
        for (entry in page.entries) output(entry.text, entry.stream == "stderr")
        return if (page.running) null else page.exitCode ?: 0
    }

    fun stop() {
        device?.let { repository.terminate(it, bundle) }
    }

    private companion object {
        const val BOOTED = "Booted"
    }
}
