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
    private val inputAllowed: () -> Boolean = { false },
    private val rememberInput: (Boolean) -> Unit = {},
    private val installer: IdbInstaller = IdbInstaller { throw IdbInstallFailed("No installer was given.", "") },
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

    /** Input was asked for and idb is not on this Mac: the menu then offers to install it. */
    var idbMissing by mutableStateOf(false)
        private set

    /** An action the person started is running: a boot, a launch, an install. */
    var working by mutableStateOf(false)
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
    private var polls = 0
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
            status = if (it.device?.state == BOOTED) "Showing ${device.name}." else "${device.name} is not running."
        }
        // The list already shows this device, so the state follows it even when the request was refused:
        // an action must never name a device the user cannot see. The service's own choice returns with
        // the running action's result or the next poll.
        selected = device
    }

    /** The devices that are running: each has a tab. */
    val running: List<SimulatorDevice> get() = devices.filter { it.state == BOOTED }

    /** Shows a device, starting it first if it is not running. */
    fun open(device: SimulatorDevice) {
        if (device.state == BOOTED) {
            selectDevice(device)
            return
        }
        if (disposed) return
        val work = {
            repository.select(device.id)
            repository.boot(device.id)
        }
        if (run("Booting ${device.name}", work, clearsPicture = true) {
                showStatus(it, fresh = true)
                status = "Booted."
            }
        ) {
            selected = device
        }
    }

    fun shutdown(device: SimulatorDevice) {
        if (disposed) return
        val work = {
            repository.select(device.id)
            repository.shutdown(device.id)
        }
        if (run("Shutting down ${device.name}", work, clearsPicture = true) {
                showStatus(it, fresh = true)
                status = "${device.name} is shut down."
            }
        ) {
            selected = device
        }
    }

    fun boot() = onDevice("Booting", clearsPicture = true, work = repository::boot) {
        showStatus(it, fresh = true)
        status = "Booted."
    }

    /** Called after the owner agreed to input through idb. */
    fun enableInput() {
        run("Switching input on", { repository.enableInput() to repository.status() }) { (found, state) ->
            if (found) inputIsOn(state, "Input is on.") else idbIsMissing(NO_IDB)
        }
    }

    /** Called after the owner agreed to what the install puts on this Mac. Its output goes to the console. */
    fun installIdb() {
        val work = {
            val said = runCatching { installer.install() }
            val found = said.isSuccess && repository.enableInput()
            Triple(said, found, if (found) repository.status() else null)
        }
        run("Installing idb", work) { (said, found, state) ->
            val failure = said.exceptionOrNull()
            append("\n[idb install]\n" + (said.getOrNull() ?: (failure as? IdbInstallFailed)?.output.orEmpty()) + "\n")
            when {
                failure != null -> idbIsMissing(failure.message ?: "The install failed.")
                state != null && found -> inputIsOn(state, "idb is installed. Input is on.")
                else -> idbIsMissing("The install finished, but idb was not found afterwards. See the console.")
            }
        }
    }

    private fun inputIsOn(state: SimulatorStatus, say: String) {
        idbMissing = false
        rememberInput(true)
        showStatus(state, fresh = false)
        status = say
    }

    private fun idbIsMissing(say: String) {
        idbMissing = true
        status = say
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
        // A device started or stopped elsewhere shows up here: the list is cheap, but not free.
        val list = ++polls % LIST_EVERY == 0
        polling.submit({ readState(from, list) }) { result ->
            if (disposed) return@submit
            val state = result.getOrElse { Polled(failure = it) }
            state.page?.let(::showPage)
            if (epoch != pictureEpoch) return@submit
            state.devices?.let(::listDevices)
            state.status?.let { showStatus(it, fresh = false) }
            state.frame?.takeIf { it.device == selected?.id }?.let {
                frame = it
                capture = "${it.captureMs} ms"
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
        val devices: List<SimulatorDevice>? = null,
        val failure: Throwable? = null,
    )

    /** Runs on the polling worker. One failing read must not hide the others, so each is caught separately. */
    private fun readState(from: Long, list: Boolean): Polled {
        var failure: Throwable? = null
        val found = if (list) runCatching { repository.devices() }.onFailure { failure = it }.getOrNull() else null
        val status = runCatching { repository.status() }.onFailure { failure = it }.getOrNull()
        // A device that is not running has no screen; asking would only fail twice a second.
        val frame = status?.device?.takeIf { it.state == BOOTED }?.let { device ->
            runCatching { repository.screenshot(device.id) }.onFailure { failure = it }.getOrNull()
        }
        val page = runCatching { repository.consolePage(from) }.onFailure { failure = failure ?: it }.getOrNull()
        return Polled(status, frame, page, found, failure)
    }

    private fun connect(): Boolean = actions.submit({
        val file = repository.connect()
        // The owner switched input on in an earlier session; a missing idb only leaves it off.
        if (inputAllowed()) repository.enableInput()
        file to repository.devices()
    }) { result ->
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

    private fun <T> run(label: String, work: () -> T, clearsPicture: Boolean = false, success: (T) -> Unit): Boolean {
        if (disposed) return false
        if (!connected) {
            status = "The local Simulator service is still starting."
            return false
        }
        val started = actions.submit(work) { result ->
            working = false
            if (!disposed) result.fold(success, ::showFailure)
        }
        if (!started) {
            status = BUSY
            return false
        }
        working = true
        status = "$label…"
        pollFailed = false
        lastError = null
        if (clearsPicture) clearPicture()
        return true
    }

    private fun listDevices(found: List<SimulatorDevice>) {
        devices = found
        val current = selected ?: return
        selected = found.firstOrNull { it.id == current.id }
        if (selected?.state != BOOTED) clearPicture()
    }

    private fun showDevices(found: List<SimulatorDevice>) {
        listDevices(found)
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
        const val BOOTED = "Booted"
        const val LIST_EVERY = 6
        const val NO_IDB = "idb is not installed. Choose Install idb… in the menu."
        const val BUSY = "Another Simulator action is still running. Try again when it finishes."
        const val TAP_SLOP = 6
        const val MAX_STATUS = 300
        const val MAX_CONSOLE = 128 * 1024
    }
}
