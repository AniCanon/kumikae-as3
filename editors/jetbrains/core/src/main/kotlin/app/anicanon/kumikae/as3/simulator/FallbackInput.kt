package app.anicanon.kumikae.as3.simulator

class FallbackInput(private val candidates: List<Pair<String, InputBackend>>) : InputBackend {
    @Volatile private var index = 0

    @Volatile private var reason = ""
    private val current: InputBackend get() = candidates.getOrNull(index)?.second ?: NoInput
    override val enabled: Boolean get() = current.enabled
    override val continuousTouch: Boolean get() = current.continuousTouch
    override val notice: String get() {
        val candidate = candidates.getOrNull(index)
        val cause = reason
        return when {
            candidate == null || !candidate.second.enabled -> "Using public screenshots. $cause"
            cause.isNotEmpty() -> "Using ${candidate.first}. $cause"
            else -> candidate.second.notice
        }
    }
    override fun statusScale(device: String): Double = current.statusScale(device)
    override fun scale(device: String): Double = opening { it.scale(device) }
    override fun frames(device: String, bind: (Geometry, Geometry) -> Long?, stopped: () -> Unit): FrameFeed =
        opening { it.frames(device, bind, stopped) }

    @Synchronized private fun <T> opening(start: (InputBackend) -> T): T {
        while (index < candidates.size) {
            if (Thread.currentThread().isInterrupted) throw InterruptedException("Simulator startup was cancelled")
            val (name, backend) = candidates[index]
            try {
                return start(backend)
            } catch (failure: Exception) {
                if (failure is InterruptedException || Thread.currentThread().isInterrupted) throw failure
                if (failure is SimulatorException && failure.code == "DEVICE_UNAVAILABLE") throw failure
                backend.close()
                reason = "$name unavailable: ${failure.message}".take(500)
                index++
            }
        }
        throw SimulatorException("INPUT_UNAVAILABLE", notice)
    }

    // Once input has been submitted, its receipt may be unknown. Never retry it on another backend.
    override fun send(device: String, operation: String, args: List<String>) = current.send(device, operation, args)
    override fun releaseIdle() = current.releaseIdle()

    @Synchronized override fun close() {
        candidates.drop(index).forEach { it.second.close() }
    }
}
