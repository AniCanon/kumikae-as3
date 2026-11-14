package app.anicanon.kumikae.as3.panel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.simulator.Pixel
import com.intellij.openapi.Disposable
import kotlin.math.abs

/** Owns Simulator requests and every piece of window state. It never constructs or touches a Swing component. */
internal class SimulatorWindowController(
    private val repository: SimulatorRepository,
    private val actions: BackgroundTasks,
    private val polling: BackgroundTasks,
) : Disposable {
    var status by mutableStateOf("Starting the local Simulator service…")
        private set
    var notice by mutableStateOf("Viewer mode. Input needs an explicitly enabled idb installation.")
        private set
    var endpointFile by mutableStateOf("")
        private set
    var devices by mutableStateOf(emptyList<SimulatorDevice>())
        private set
    var selected by mutableStateOf<SimulatorDevice?>(null)
        private set
    var frame by mutableStateOf<SimulatorFrame?>(null)
        private set
    var capture by mutableStateOf("")
        private set
    var inputEnabled by mutableStateOf(false)
        private set
    var console by mutableStateOf("")
        private set

    private var connected = false
    private var visible = false
    private var disposed = false
    private var cursor = 0L

    // Counts every change that makes an in-flight picture out of date. It is not the service's geometry generation.
    private var pictureEpoch = 0L
    private var pollFailed = false
    private var lastError: String? = null

    fun shown() {
        if (disposed) return
        visible = true
        if (!connected && !actions.isBusy) connect()
    }

    fun hidden() {
        visible = false
    }

    fun refreshDevices() {
        if (!connected) {
            if (!disposed && !connect()) status = BUSY
            return
        }
        run("Refreshing devices", { repository.devices() }) { showDevices(it) }
    }

    fun selectDevice(device: SimulatorDevice) {
        if (disposed || device.id == selected?.id) return
        run("Selecting ${device.name}", { repository.select(device.id) }, clearsPicture = true) {
            showStatus(it, fresh = true)
            status = "Selected ${device.name}. Boot it if it is not running."
        }
        // The list already shows this device, so the state follows it even when the request was refused:
        // an action must never name a device the user cannot see. The service's own choice returns with
        // the running action's result or the next poll.
        selected = device
    }

    fun boot() = onDevice("Booting", clearsPicture = true, work = repository::boot) {
        showStatus(it, fresh = true)
        status = "Booted."
    }

    fun install(appPath: String) = onDevice("Installing", work = { repository.install(it, appPath.trim()) }) {
        status = "Installed."
    }

    fun launch(bundle: String) = onDevice(
        "Launching",
        clearsPicture = true,
        work = { repository.launch(it, bundle.trim()) },
    ) {
        status = "Launch requested. Check the console for success or failure."
    }

    fun send(input: SimulatorInput) = onDevice(
        "Sending input",
        clearsPicture = input is SimulatorInput.Rotate,
        work = { repository.input(it, input) },
    ) {
        status = "Input sent."
    }

    fun gesture(on: SimulatorFrame, start: Pixel, end: Pixel, durationMs: Long) {
        if (!inputEnabled || on.device != selected?.id) return
        val tap = abs(start.x - end.x) + abs(start.y - end.y) < TAP_SLOP
        send(
            if (tap) {
                SimulatorInput.Tap(on.generation, on.geometry, start)
            } else {
                SimulatorInput.Swipe(on.generation, on.geometry, start, end, durationMs)
            },
        )
    }

    fun poll() {
        if (disposed || !visible || !connected || actions.isBusy || polling.isBusy) return
        val epoch = pictureEpoch
        val from = cursor
        polling.submit({ readState(from) }) { result ->
            if (disposed) return@submit
            val state = result.getOrElse { Polled(failure = it) }
            state.page?.let(::showPage)
            if (epoch != pictureEpoch) return@submit
            state.status?.let { showStatus(it, fresh = false) }
            state.frame?.takeIf { it.device == selected?.id }?.let {
                frame = it
                capture = "Snapshot · ${it.captureMs} ms capture · up to 2 a second"
            }
            val failure = state.failure
            if (failure != null) {
                pollFailed = true
                showFailure(failure)
            } else if (pollFailed) {
                pollFailed = false
                lastError = null
                status = "Connected."
            }
        }
    }

    override fun dispose() {
        if (disposed) return
        disposed = true
        actions.dispose()
        polling.dispose()
        repository.close()
    }

    private class Polled(
        val status: SimulatorStatus? = null,
        val frame: SimulatorFrame? = null,
        val page: ConsolePage? = null,
        val failure: Throwable? = null,
    )

    /** Runs on the polling worker. One failing read must not hide the others, so each is caught separately. */
    private fun readState(from: Long): Polled {
        var failure: Throwable? = null
        val status = runCatching { repository.status() }.onFailure { failure = it }.getOrNull()
        val frame = status?.device?.let { device ->
            runCatching { repository.screenshot(device.id) }.onFailure { failure = it }.getOrNull()
        }
        val page = runCatching { repository.consolePage(from) }.onFailure { failure = failure ?: it }.getOrNull()
        return Polled(status, frame, page, failure)
    }

    private fun connect(): Boolean = actions.submit({ repository.connect() to repository.devices() }) { result ->
        if (disposed) return@submit
        result.fold({ (file, found) ->
            connected = true
            endpointFile = file
            showDevices(found)
        }, ::showFailure)
    }

    private fun <T> onDevice(
        label: String,
        clearsPicture: Boolean = false,
        work: (String) -> T,
        success: (T) -> Unit,
    ) {
        if (disposed) return
        val device = selected?.id
        if (device == null) {
            status = "Select a device first."
        } else {
            run(label, { work(device) }, clearsPicture, success)
        }
    }

    private fun <T> run(label: String, work: () -> T, clearsPicture: Boolean = false, success: (T) -> Unit) {
        if (disposed) return
        if (!connected) {
            status = "The local Simulator service is still starting."
            return
        }
        val started = actions.submit(work) { result ->
            if (!disposed) result.fold(success, ::showFailure)
        }
        if (!started) {
            status = BUSY
            return
        }
        status = "$label…"
        pollFailed = false
        lastError = null
        if (clearsPicture) clearPicture()
    }

    private fun showDevices(found: List<SimulatorDevice>) {
        devices = found
        selected = selected?.let { current -> found.firstOrNull { it.id == current.id } }
        if (selected == null) clearPicture()
        status = if (found.isEmpty()) {
            "No available iOS devices. Add one in Xcode."
        } else {
            "Select a device, then Boot if needed."
        }
    }

    /**
     * A poll's status repeats the device as it was when it was selected, so only its identity is followed.
     * The reply to a select or boot is `fresh`: its device state is newer than the list's.
     */
    private fun showStatus(value: SimulatorStatus, fresh: Boolean) {
        inputEnabled = value.inputEnabled
        notice = value.notice
        val device = value.device
        if (device?.id != selected?.id) clearPicture()
        if (device != null && fresh) devices = devices.map { if (it.id == device.id) device else it }
        selected = device?.let { reported -> devices.firstOrNull { it.id == reported.id } ?: reported }
    }

    private fun showPage(page: ConsolePage) {
        cursor = page.nextCursor
        val text = page.entries.joinToString("") { "[${it.stream}] ${it.text}" }
        append((if (page.truncated) "\n[Older output discarded]\n" else "") + text)
    }

    private fun showFailure(failure: Throwable) {
        clearPicture()
        val message = (failure.message ?: failure.javaClass.simpleName).take(MAX_STATUS)
        status = message
        if (message != lastError) {
            lastError = message
            append("\n[Error] $message\n")
        }
    }

    private fun clearPicture() {
        pictureEpoch++
        frame = null
        capture = ""
    }

    private fun append(text: String) {
        if (text.isNotEmpty()) console = (console + text).takeLast(MAX_CONSOLE)
    }

    private companion object {
        const val BUSY = "Another Simulator action is still running. Try again when it finishes."
        const val TAP_SLOP = 6
        const val MAX_STATUS = 300
        const val MAX_CONSOLE = 128 * 1024
    }
}
