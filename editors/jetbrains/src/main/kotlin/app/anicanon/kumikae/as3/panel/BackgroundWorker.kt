package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ModalityState
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.atomic.AtomicBoolean

/** Runs one request at a time away from the UI thread and delivers its result on the UI thread. */
internal interface BackgroundTasks : Disposable {
    val isBusy: Boolean

    /** Returns false, without running anything, while an earlier request is unfinished or after disposal. */
    fun <T> submit(work: () -> T, completed: (Result<T>) -> Unit): Boolean
}

/** Window-scoped worker. Disposal interrupts the running request and suppresses queued UI callbacks. */
internal class BackgroundWorker(private val threadName: String) : BackgroundTasks {
    private val worker = Executors.newSingleThreadExecutor { task ->
        Thread(task, threadName).apply { isDaemon = true }
    }
    private val disposed = AtomicBoolean()
    private val busy = AtomicBoolean()
    private val lock = Any()
    private var pending: Future<*>? = null

    override val isBusy: Boolean get() = busy.get()

    override fun <T> submit(work: () -> T, completed: (Result<T>) -> Unit): Boolean {
        synchronized(lock) {
            if (disposed.get() || !busy.compareAndSet(false, true)) return false
            pending = worker.submit {
                val result = try {
                    Result.success(work())
                } catch (interrupted: InterruptedException) {
                    Thread.currentThread().interrupt()
                    Result.failure(interrupted)
                } catch (failure: Exception) {
                    Result.failure(failure)
                }
                ApplicationManager.getApplication().invokeLater({
                    busy.set(false)
                    if (!disposed.get()) completed(result)
                }, ModalityState.any())
            }
            return true
        }
    }

    override fun dispose() {
        synchronized(lock) {
            if (!disposed.compareAndSet(false, true)) return
            pending?.cancel(true)
            worker.shutdownNow()
        }
    }
}
