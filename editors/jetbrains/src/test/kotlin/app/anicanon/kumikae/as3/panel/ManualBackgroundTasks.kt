package app.anicanon.kumikae.as3.panel

/** Separates work from delivery so tests can reproduce late completions without sleeps. */
internal class ManualBackgroundTasks : BackgroundTasks {
    override var isBusy = false
        private set
    var disposed = false
        private set
    private var work: (() -> Unit)? = null
    private var delivery: (() -> Unit)? = null

    override fun <T> submit(work: () -> T, completed: (Result<T>) -> Unit): Boolean {
        if (isBusy || disposed) return false
        isBusy = true
        this.work = {
            val result = runCatching(work)
            delivery = { completed(result) }
        }
        return true
    }

    fun runWork() {
        checkNotNull(work) { "No work was submitted" }.invoke()
        work = null
    }

    fun deliver() {
        val callback = checkNotNull(delivery) { "No result is waiting" }
        delivery = null
        isBusy = false
        callback()
    }

    fun finish() {
        runWork()
        deliver()
    }

    // Keep delivery available deliberately: controllers must reject callbacks after disposal too.
    override fun dispose() {
        disposed = true
    }
}
