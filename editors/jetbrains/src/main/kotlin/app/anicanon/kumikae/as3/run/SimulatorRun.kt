package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.panel.SimulatorRepository
import java.nio.file.Files
import java.nio.file.Path

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
    private val destination: String? = null,
    // Whether an app that is named is there to install.
    private val exists: (String) -> Boolean = { Files.isDirectory(Path.of(it)) },
    // Builds the game for the Simulator and returns the .app, saying what it does as it goes. Null where
    // there is nothing to build from.
    private val build: (((String, Boolean) -> Unit) -> String)? = null,
) {
    private val bundle = bundle.trim()
    private val appPath = appPath.trim()

    @Volatile private var device: String? = null

    @Volatile private var cancelled = false

    @Volatile private var launched = false
    private var cursor = 0L
    private var explained = false

    fun start() {
        check(bundle.isNotEmpty()) { "Set the app's bundle identifier in the run configuration." }
        if (cancelled) return
        repository.connect()
        if (cancelled) return
        val chosen = if (destination != null) {
            val selected =
                requireNotNull(repository.select(destination).device) { "The selected simulator is unavailable." }
            if (selected.state != BOOTED) repository.boot(selected.id)
            selected
        } else {
            repository.status().device?.takeIf { it.state == BOOTED }
                ?: repository.devices().firstOrNull { it.state == BOOTED }?.also { repository.select(it.id) }
                ?: error("No simulator is running. Boot one in the iOS Simulator panel, then run again.")
        }
        device = chosen.id
        if (cancelled) return
        // An app that is named is installed as it is. With none named, or with one named that is not there
        // (its build folder was cleared), the game is built now: Run means "what I have just written", as it
        // does everywhere else.
        val app = when {
            appPath.isEmpty() -> build?.invoke(output).orEmpty()

            exists(appPath) -> appPath

            else -> {
                val builder = build ?: error(MISSING.replace("{app}", appPath))
                output("There is no app at $appPath: building the game for the Simulator.\n", false)
                builder(output)
            }
        }
        if (cancelled) return
        if (app.isNotEmpty()) repository.install(chosen.id, app)
        // Output from before this launch belongs to another run.
        cursor = repository.consolePage(0).nextCursor
        synchronized(this) {
            if (cancelled) return
            repository.launch(chosen.id, bundle, sources)
            launched = true
        }
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

    @Synchronized fun stop() {
        cancelled = true
        if (!launched) return
        launched = false
        device?.let { repository.terminate(it, bundle) }
    }

    private companion object {
        const val BOOTED = "Booted"
        const val MISSING = "There is no app at {app}. Build the game for the Simulator, or clear the app in this " +
            "run configuration so that Run builds it."
        const val NOT_INSTALLED = "{bundle} is not installed on this simulator. Export the game for iOS from Godot, " +
            "build it for the Simulator, and name the built .app in this run configuration: it is installed before " +
            "the launch.\n"
    }
}
