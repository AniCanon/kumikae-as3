package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.panel.BackgroundTasks
import com.intellij.openapi.Disposable

/** Discovery state only. Native IDE target selection remains owned by ExecutionTargetManager. */
internal class IosTargetController(
    private val discover: () -> IosDeviceSnapshot,
    private val tasks: BackgroundTasks,
    private val changed: () -> Unit,
    private val now: () -> Long = System::nanoTime,
) : Disposable {
    @Volatile var snapshot = IosDeviceSnapshot(emptyList(), emptyList())
        private set

    @Volatile var loading = false
        private set
    private var disposed = false
    private var nextRefreshAt = Long.MIN_VALUE

    fun refresh() {
        if (disposed || loading || now() < nextRefreshAt) return
        loading = true
        if (!tasks.submit(discover) { result ->
                if (!disposed) {
                    loading = false
                    nextRefreshAt = now() + REFRESH_NANOS
                    val updated = result.getOrElse {
                        IosDeviceSnapshot(emptyList(), listOf(it.message ?: "Device discovery failed"))
                    }
                    if (snapshot != updated) {
                        snapshot = updated
                        changed()
                    }
                }
            }
        ) {
            loading = false
        }
    }

    private companion object {
        const val REFRESH_NANOS = 5_000_000_000L
    }

    override fun dispose() {
        disposed = true
        tasks.dispose()
    }
}
