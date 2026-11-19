package app.anicanon.kumikae.as3.panel

import java.util.concurrent.LinkedBlockingDeque

internal class AndroidPointerQueue {
    private val events = LinkedBlockingDeque<String>()
    private var closed = false

    @Synchronized fun offer(phase: String, x: Int, y: Int) {
        require(phase in PHASES) { "Unsupported pointer phase: $phase" }
        if (closed) return
        if (phase == "MOVE" && events.peekLast()?.startsWith("MOVE ") == true) events.pollLast()
        events.add("$phase $x $y")
    }

    fun take(): String = events.take()

    @Synchronized fun close() {
        if (!closed) {
            closed = true
            events.add("")
        }
    }

    private companion object {
        val PHASES = setOf("DOWN", "MOVE", "UP")
    }
}
