package app.anicanon.kumikae.as3.language

/** One bounded refresh per project; scheduled callbacks may arrive after cancellation. */
internal class DeclarationRefresh(
    private val schedule: (Long, () -> Unit) -> Unit,
    private val cancel: () -> Unit,
    private val refresh: () -> Boolean,
) {
    private val lock = Any()
    private var started = false
    private var closed = false
    private var attempts = 0

    fun start() = synchronized(lock) {
        if (!started && !closed) {
            started = true
            schedule(0, ::attempt)
        }
    }

    private fun attempt() {
        synchronized(lock) {
            if (closed) return
            attempts++
        }
        val found = refresh()
        synchronized(lock) {
            if (!closed && !found && attempts < MAX_ATTEMPTS) schedule(RETRY_MILLIS, ::attempt)
        }
    }

    fun close() = synchronized(lock) {
        if (!closed) {
            closed = true
            cancel()
        }
    }

    private companion object {
        const val MAX_ATTEMPTS = 20
        const val RETRY_MILLIS = 500L
    }
}
