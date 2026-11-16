package app.anicanon.kumikae.as3.simulator

interface InputBackend : AutoCloseable {
    val enabled: Boolean
    val continuousTouch: Boolean get() = false
    val notice: String get() = "Live picture and input are on through the native helper."

    /** Screenshot pixels for one of the device's points: input is sent in points. */
    fun scale(device: String): Double
    fun statusScale(device: String): Double = scale(device)

    fun supports(device: String): Boolean = enabled

    fun send(device: String, operation: String, args: List<String>)

    fun releaseIdle() = Unit

    fun frames(device: String, bind: (Geometry, Geometry) -> Long?, stopped: () -> Unit): FrameFeed =
        throw SimulatorException("INPUT_DISABLED", "Live picture needs the owner's private-input switch")

    override fun close() = Unit
}

object NoInput : InputBackend {
    override val enabled = false

    override fun scale(device: String) = 1.0

    override fun send(device: String, operation: String, args: List<String>): Nothing = throw SimulatorException(
        "INPUT_DISABLED",
        "Input is off. Switch it on in the panel's menu.",
    )
}
