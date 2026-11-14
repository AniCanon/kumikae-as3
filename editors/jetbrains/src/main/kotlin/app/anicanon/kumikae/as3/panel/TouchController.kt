package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel

internal class TouchController(
    private val tasks: BackgroundTasks,
    private val send: (SimulatorFrame, String, Pixel) -> Unit,
    private val failed: (Throwable) -> Unit,
) {
    private data class Event(val frame: SimulatorFrame, val phase: String, val at: Pixel)
    private var active: Event? = null
    private var pending: Event? = null
    private var releasing = false
    private var disposed = false

    fun offer(frame: SimulatorFrame, phase: String, at: Pixel) {
        if (disposed) return
        if (phase == "down") {
            if (active != null || tasks.isBusy) {
                failed(IllegalStateException("The previous touch is still finishing."))
                return
            }
            active = Event(frame, phase, at)
            releasing = false
        } else {
            val current = active ?: return
            if (releasing || current.frame.device != frame.device ||
                current.frame.generation != frame.generation
            ) {
                return
            }
            if (phase == "up") releasing = true
            active = Event(current.frame, phase, at)
        }
        pending = Event(frame, phase, at)
        drain()
    }

    fun cancel() {
        val current = active ?: return
        if (releasing) return
        offer(current.frame, "up", current.at)
    }

    /** Closing the frame feed releases the helper's finger even when a worker is interrupted. */
    fun dispose() {
        disposed = true
        pending = null
        active = null
        tasks.dispose()
    }

    private fun drain() {
        if (disposed || tasks.isBusy) return
        val next = pending ?: return
        pending = null
        tasks.submit({ send(next.frame, next.phase, next.at) }) { result ->
            if (disposed) return@submit
            if (result.isFailure) {
                pending = null
                active = null
                failed(checkNotNull(result.exceptionOrNull()))
            } else if (next.phase == "up") {
                active = null
                pending = null
            } else {
                drain()
            }
        }
    }
}
