package app.anicanon.kumikae.as3.panel

/** The typed boundary between the tool window and the local Simulator service. Every call may block. */
internal interface SimulatorRepository : AutoCloseable {
    /** Starts the local service if it is not running and returns the agent endpoint file. */
    fun connect(): String

    fun devices(): List<SimulatorDevice>

    fun status(): SimulatorStatus

    fun select(device: String): SimulatorStatus

    fun boot(device: String): SimulatorStatus

    fun install(device: String, appPath: String)

    fun launch(device: String, bundle: String)

    fun screenshot(device: String): SimulatorFrame

    fun consolePage(cursor: Long): ConsolePage

    fun input(device: String, input: SimulatorInput)
}
