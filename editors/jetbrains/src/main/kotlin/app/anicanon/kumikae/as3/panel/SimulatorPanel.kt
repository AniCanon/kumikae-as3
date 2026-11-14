package app.anicanon.kumikae.as3.panel

import com.google.gson.JsonElement
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.PathManager
import app.anicanon.kumikae.as3.simulator.*
import java.awt.BorderLayout
import java.awt.FlowLayout
import java.awt.GridLayout
import java.awt.event.HierarchyEvent
import java.io.ByteArrayInputStream
import java.nio.file.Path
import java.util.Base64
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import javax.imageio.ImageIO
import javax.swing.*

private data class DeviceChoice(val id: String, val label: String) { override fun toString() = label }

class SimulatorPanel : JPanel(BorderLayout()), Disposable {
    private val worker = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "kumikae-simulator-panel").apply { isDaemon = true }
    }
    private val lifecycle = Any()
    private var session: LocalSession? = null
    private var client: ApiClient? = null
    private var selected: String? = null
    private var loadingDevices = false
    @Volatile private var visiblePanel = false
    @Volatile private var disposed = false
    private val actionPending = AtomicBoolean()
    private val devices = JComboBox<DeviceChoice>()
    private val status = JLabel("Starting local session…")
    private val notice = JTextArea("Viewer mode. Input needs an explicitly enabled idb installation.")
    private val endpoint = JTextField().apply { isEditable = false }
    private val appPath = JTextField(18)
    private val bundle = JTextField("org.kumikae.as3test", 18)
    private val text = JTextField(16)
    private val logs = JTextArea().apply { isEditable = false; lineWrap = true }
    private val inputControls = mutableListOf<JComponent>()
    private var cursor = 0L
    private var lastPoll = 0L
    private var lastError: String? = null
    private val screen = SimulatorScreen(::gesture)

    init {
        notice.isEditable = false
        notice.lineWrap = true
        notice.wrapStyleWord = true
        notice.background = background
        val controls = JPanel(GridLayout(0, 1, 0, 3))
        controls.add(row(devices, button("Refresh") { refreshDevices() }, button("Boot") { request("boot") }))
        controls.add(row(JLabel("App"), appPath, button("Install") {
            request("install", mapOf("path" to appPath.text))
        }))
        controls.add(row(JLabel("Bundle"), bundle, button("Launch") {
            request("launch", mapOf("bundle" to bundle.text))
        }))
        val orientation = JComboBox(arrayOf("portrait", "landscape_left", "landscape_right", "portrait_upside_down"))
        controls.add(row(inputButton("Home") { request("button", mapOf("button" to "home")) },
            inputButton("Lock") { request("button", mapOf("button" to "lock")) }, orientation,
            inputButton("Rotate") { request("rotate", mapOf("orientation" to orientation.selectedItem.toString())) }))
        controls.add(row(text, inputButton("Type") { request("text", mapOf("text" to text.text)) }))
        controls.add(notice)
        controls.add(status)
        add(controls, BorderLayout.NORTH)
        val split = JSplitPane(JSplitPane.VERTICAL_SPLIT, screen, JScrollPane(logs))
        split.resizeWeight = 0.75
        add(split, BorderLayout.CENTER)
        add(JPanel(BorderLayout()).apply {
            add(JLabel("Agent endpoint file"), BorderLayout.NORTH)
            add(endpoint, BorderLayout.CENTER)
        }, BorderLayout.SOUTH)
        devices.addActionListener { if (!loadingDevices) selectDevice() }
        addHierarchyListener { if (it.changeFlags and HierarchyEvent.SHOWING_CHANGED.toLong() != 0L) visiblePanel = isShowing }
        submit {
            val local = LocalSession(Path.of(PathManager.getSystemPath(), "kumikae-as3"))
            synchronized(lifecycle) {
                if (disposed) { local.close(); return@submit }
                session = local
                client = ApiClient(local.server.endpoint)
            }
            ui { endpoint.text = local.server.endpointFile.toString() }
            loadDevices()
        }
        worker.scheduleWithFixedDelay({ poll() }, 100, 100, TimeUnit.MILLISECONDS)
    }

    private fun row(vararg components: JComponent) = JPanel(FlowLayout(FlowLayout.LEFT, 4, 0)).apply {
        components.forEach { add(it) }
    }
    private fun button(label: String, action: () -> Unit) = JButton(label).apply { addActionListener { action() } }
    private fun inputButton(label: String, action: () -> Unit) = button(label, action).also {
        it.isEnabled = false; inputControls.add(it)
    }

    private fun refreshDevices() = submit { loadDevices() }
    private fun loadDevices() {
        val values = api().call("devices").asJsonArray.map {
            val d = it.asJsonObject
            DeviceChoice(d["id"].asString, "${d["name"].asString} · ${d["runtime"].asString} · ${d["state"].asString}")
        }
        ui {
            val old = (devices.selectedItem as? DeviceChoice)?.id
            loadingDevices = true
            devices.removeAllItems()
            values.forEach(devices::addItem)
            devices.selectedItem = values.find { it.id == old }
            loadingDevices = false
            status.text = if (values.isEmpty()) "No available iOS devices. Add one in Xcode." else "Select a device, then Boot if needed."
            if (devices.selectedItem == null) screen.frame = null
        }
    }

    private fun selectDevice() {
        val choice = devices.selectedItem as? DeviceChoice ?: return
        screen.frame = null
        submit {
            val result = api().call("select", mapOf("device" to choice.id))
            selected = choice.id
            showStatus(result)
        }
    }

    private fun request(method: String, params: Map<String, Any> = emptyMap()) = submit {
        val id = selected ?: throw IllegalStateException("Select a device first")
        val result = api().call(method, params + ("device" to id))
        ui {
            if (method in setOf("rotate", "launch", "boot")) screen.frame = null
            status.text = if (method == "launch") "Launch requested. Check logs for success or failure." else "$method completed"
        }
        if (method == "boot") showStatus(result)
    }

    private fun gesture(frame: ScreenFrame, start: Pixel, end: Pixel, duration: Long) {
        val params = mutableMapOf<String, Any>("device" to frame.device, "generation" to frame.generation,
            "width" to frame.geometry.width, "height" to frame.geometry.height, "x" to start.x, "y" to start.y)
        val tap = kotlin.math.abs(start.x - end.x) + kotlin.math.abs(start.y - end.y) < 6
        if (!tap) params.putAll(mapOf("endX" to end.x, "endY" to end.y, "durationMs" to duration.coerceIn(50, 5000)))
        submit { api().call(if (tap) "tap" else "swipe", params) }
    }

    private fun poll() {
        if (disposed || !visiblePanel || actionPending.get() || client == null) return
        val now = System.nanoTime()
        if (now - lastPoll < 500_000_000) return
        lastPoll = now
        try {
            val state = api().call("status")
            showStatus(state)
            val device = state.asJsonObject["device"]
            selected = if (device == null || device.isJsonNull) null else device.asJsonObject["id"].asString
            selected?.let { id ->
                val frame = api().call("screenshot", mapOf("device" to id, "format" to "jpeg")).asJsonObject
                val image = ImageIO.read(ByteArrayInputStream(Base64.getDecoder().decode(frame["data"].asString)))
                    ?: error("Invalid screenshot")
                ui {
                    screen.frame = ScreenFrame(id, frame["generation"].asLong, image)
                    status.text = "Snapshot · ${frame["captureMs"].asLong} ms capture · up to 2 fps"
                }
            }
        } catch (e: Exception) { showError(e) }
        try {
            val page = api().call("log", mapOf("cursor" to cursor)).asJsonObject.getAsJsonObject("page")
            cursor = page["nextCursor"].asLong
            val output = page.getAsJsonArray("entries").joinToString("") {
                val entry = it.asJsonObject
                "[${entry["stream"].asString}] ${entry["text"].asString}"
            }
            ui {
                if (page["truncated"].asBoolean) logs.append("\n[Older output discarded]\n")
                logs.append(output)
                if (logs.document.length > 128 * 1024) logs.document.remove(0, logs.document.length - 128 * 1024)
            }
        } catch (e: Exception) { showError(e) }
    }

    private fun showStatus(result: JsonElement) {
        val state = result.asJsonObject
        ui {
            val enabled = state["inputEnabled"].asBoolean
            inputControls.forEach { it.isEnabled = enabled }
            screen.inputEnabled = enabled
            notice.text = state["inputNotice"].asString
            val device = state["device"]
            if (device != null && !device.isJsonNull) {
                val id = device.asJsonObject["id"].asString
                loadingDevices = true
                devices.selectedItem = (0 until devices.itemCount).map { devices.getItemAt(it) }.find { it.id == id }
                loadingDevices = false
            }
        }
    }

    private fun api() = client ?: error("Local session is still starting")
    private fun submit(work: () -> Unit) {
        if (disposed || !actionPending.compareAndSet(false, true)) return
        try {
            worker.execute {
                try { work() } catch (e: Exception) { showError(e) }
                finally { actionPending.set(false) }
            }
        } catch (_: java.util.concurrent.RejectedExecutionException) { actionPending.set(false) }
    }
    private fun showError(error: Exception) = ui {
        screen.frame = null
        status.text = (error.message ?: error.javaClass.simpleName).take(300)
        status.toolTipText = status.text
        if (lastError != status.text) {
            lastError = status.text
            logs.append("\n[Error] ${status.text}\n")
            if (logs.document.length > 128 * 1024) logs.document.remove(0, logs.document.length - 128 * 1024)
        }
    }
    private fun ui(work: () -> Unit) = SwingUtilities.invokeLater { if (!disposed) work() }

    override fun dispose() {
        val local = synchronized(lifecycle) { disposed = true; session.also { session = null } }
        worker.shutdownNow()
        Thread({ local?.close() }, "kumikae-simulator-close").apply { isDaemon = true }.start()
    }
}
