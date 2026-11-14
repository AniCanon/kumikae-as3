package app.anicanon.kumikae.as3.panel

/** The window controller's live-picture lifetime. All methods and deliveries run on the EDT. */
internal class PictureStreamController(
    private val repository: SimulatorRepository,
    private val tasks: BackgroundTasks,
    private val clock: () -> Long,
    private val show: (SimulatorFrame?) -> Unit,
    private val say: (String) -> Unit,
) {
    private var feed: SimulatorFrames? = null
    private var epoch = 0L
    private var device: String? = null
    private var retryAt = 0L
    private var retryMs = FIRST_RETRY_MS
    private var disposed = false

    fun tick(wanted: String?) {
        if (disposed) return
        if (wanted != device) {
            stop()
            device = wanted
        }
        if (wanted == null) return
        feed?.let { current ->
            if (current.failure != null || current.ended) {
                failed(current.failure ?: IllegalStateException("The session changed"))
            }
        }
        feed?.poll()?.let {
            if (it.device == wanted) {
                retryMs = FIRST_RETRY_MS
                show(it)
            }
        }
        if (feed != null || tasks.isBusy || clock() < retryAt) return
        val started = epoch
        tasks.submit({ repository.openFrames(wanted) }) { result ->
            if (disposed || started != epoch) {
                result.getOrNull()?.close()
                return@submit
            }
            result.fold({
                feed = it
                say("Live picture connected.")
            }, ::failed)
        }
    }

    fun stop() {
        epoch++
        feed?.close()
        feed = null
        device = null
        retryAt = 0
        retryMs = FIRST_RETRY_MS
    }

    fun dispose() {
        disposed = true
        stop()
        tasks.dispose()
    }

    private fun failed(error: Throwable) {
        show(null)
        feed?.close()
        feed = null
        // What a person needs to know is that the picture will come back, not when to the millisecond.
        say("Reconnecting to the simulator's screen…")
        retryAt = clock() + retryMs
        retryMs = (retryMs * 2).coerceAtMost(MAX_RETRY_MS)
    }

    private companion object {
        const val FIRST_RETRY_MS = 500L
        const val MAX_RETRY_MS = 8000L
    }
}
