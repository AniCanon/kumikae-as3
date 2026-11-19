package app.anicanon.kumikae.as3.panel

import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean

/** The emulator exports one producer per device, shared by all open project windows. */
internal object AndroidCaptureLease {
    private data class Producer(var readers: Int, val stop: () -> Unit)
    private val locks = ConcurrentHashMap<String, Any>()
    private val producers = mutableMapOf<String, Producer>()
    private val stopped = mutableSetOf<String>()

    private fun lockFor(key: String) = locks.computeIfAbsent(key) { Any() }

    fun acquire(key: String, active: () -> Boolean, start: () -> Unit, stop: () -> Unit): AutoCloseable {
        val lock = lockFor(key)
        val producer = synchronized(lock) {
            producers[key]?.also { it.readers++ } ?: run {
                // Stopping recording leaves the shared-memory name allocated in the emulator.
                val wasStopped = key in stopped
                val owned = wasStopped || !active()
                start()
                stopped.remove(key)
                Producer(1) {
                    if (owned) {
                        stop()
                        stopped += key
                    }
                }.also { producers[key] = it }
            }
        }
        val closed = AtomicBoolean()
        return AutoCloseable {
            synchronized(lock) {
                if (closed.compareAndSet(false, true) && --producer.readers == 0) {
                    producers.remove(key)
                    producer.stop()
                }
            }
        }
    }

    fun forget(key: String) {
        synchronized(lockFor(key)) { stopped.remove(key) }
    }
}
