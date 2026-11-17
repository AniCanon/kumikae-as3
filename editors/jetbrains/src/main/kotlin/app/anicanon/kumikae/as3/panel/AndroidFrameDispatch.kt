package app.anicanon.kumikae.as3.panel

import java.util.concurrent.atomic.AtomicBoolean

/** Deliver the newest frame as soon as the UI thread is ready, without queuing stale paints. */
internal class AndroidFrameDispatch(private val schedule: (() -> Unit) -> Unit, private val present: () -> Unit) {
    private val queued = AtomicBoolean()

    fun request() {
        if (queued.compareAndSet(false, true)) {
            schedule {
                queued.set(false)
                present()
            }
        }
    }
}
