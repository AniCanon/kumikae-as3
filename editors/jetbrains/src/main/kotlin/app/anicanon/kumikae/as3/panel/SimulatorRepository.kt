package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel

/** The typed boundary between the tool window and the local Simulator service. Every call may block. */
internal interface SimulatorRepository : AutoCloseable {
    /** Starts the local service if it is not running and returns the agent endpoint file. */
    fun connect(): String

    fun devices(): List<SimulatorDevice>

    fun status(): SimulatorStatus

    fun select(device: String): SimulatorStatus

    fun boot(device: String): SimulatorStatus

    fun shutdown(device: String): SimulatorStatus

    /** Switches input through native helper on. False when the bundled helper is missing. */
    fun enableInput(): Boolean

    fun install(device: String, appPath: String)

    fun launch(device: String, bundle: String, sources: String = "")

    /** Ends the app that was launched. */
    fun terminate(device: String, bundle: String)

    fun screenshot(device: String): SimulatorFrame

    fun openFrames(device: String): SimulatorFrames = error("Live pictures are unavailable")

    fun consolePage(cursor: Long): ConsolePage

    fun input(device: String, input: SimulatorInput)

    fun touch(on: SimulatorFrame, phase: String, at: Pixel): Unit = error("Continuous touch is unavailable")
}

/** Polling this slot never performs I/O or decodes; the service workers own that work. */
internal interface SimulatorFrames : AutoCloseable {
    fun setFrameListener(listener: (() -> Unit)?)
    fun poll(): SimulatorFrame?
    val failure: Throwable?
    val ended: Boolean
}
