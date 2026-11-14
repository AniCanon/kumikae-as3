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
    // The folder the game's scripts are edited in: the game reads them there, and takes a save while it runs.
    private val sources: String = "",
    // Builds the game for the Simulator and returns the .app, saying what it does as it goes. Null where
    // there is nothing to build from.
    private val build: (((String, Boolean) -> Unit) -> String)? = null,
) {
    private val bundle = bundle.trim()
    private val appPath = appPath.trim()
    private var device: String? = null
    private var cursor = 0L
    private var explained = false

    fun start() {
        check(bundle.isNotEmpty()) { "Set the app's bundle identifier in the run configuration." }
        repository.connect()
        val chosen = repository.status().device?.takeIf { it.state == BOOTED }
            ?: repository.devices().firstOrNull { it.state == BOOTED }?.also { repository.select(it.id) }
            ?: error("No simulator is running. Boot one in the iOS Simulator panel, then run again.")
        device = chosen.id
        // An app that is named is installed as it is. With none named, the game is built now: Run means
        // "what I have just written", as it does everywhere else.
        val app = appPath.ifEmpty { build?.invoke(output).orEmpty() }
        if (app.isNotEmpty()) repository.install(chosen.id, app)
        // Output from before this launch belongs to another run.
        cursor = repository.consolePage(0).nextCursor
        repository.launch(chosen.id, bundle, sources)
    }

    /** Reads what was printed since the last call. Returns the app's exit code once it has ended. */
    fun pump(): Int? {
        val page = repository.consolePage(cursor)
        cursor = page.nextCursor
        for (entry in page.entries) {
            output(entry.text, entry.stream == "stderr")
            // The Simulator's own words for this say nothing a person can act on.
            if (!explained && entry.text.contains("The request to open") && entry.text.contains("failed")) {
                explained = true
                output(NOT_INSTALLED.replace("{bundle}", bundle), true)
            }
        }
        return if (page.running) null else page.exitCode ?: 0
    }

    fun stop() {
        device?.let { repository.terminate(it, bundle) }
    }

    private companion object {
        const val BOOTED = "Booted"
        const val NOT_INSTALLED = "{bundle} is not installed on this simulator. Export the game for iOS from Godot, " +
            "build it for the Simulator, and name the built .app in this run configuration: it is installed before " +
            "the launch.\n"
    }
}
