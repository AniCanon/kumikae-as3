package app.anicanon.kumikae.as3.panel

/** The typed boundary between the tool window and the local Simulator service. Every call may block. */
internal interface SimulatorRepository : AutoCloseable {
    /** Starts the local service if it is not running and returns the agent endpoint file. */
    fun connect(): String

    fun devices(): List<SimulatorDevice>

    fun status(): SimulatorStatus

    fun select(device: String): SimulatorStatus

    fun boot(device: String): SimulatorStatus

    fun shutdown(device: String): SimulatorStatus

    /** Switches input through idb on. False when idb is not installed. */
    fun enableInput(): Boolean

    fun install(device: String, appPath: String)

    fun launch(device: String, bundle: String)

    /** Ends the app that was launched. */
    fun terminate(device: String, bundle: String)

    fun screenshot(device: String): SimulatorFrame

    fun openFrames(device: String): SimulatorFrames = error("Live pictures are unavailable")

    fun consolePage(cursor: Long): ConsolePage

    fun input(device: String, input: SimulatorInput)
}

/** Polling this slot never performs I/O or decodes; the service workers own that work. */
internal interface SimulatorFrames : AutoCloseable {
    fun poll(): SimulatorFrame?
    val failure: Throwable?
    val ended: Boolean
}
