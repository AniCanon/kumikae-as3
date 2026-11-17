package app.anicanon.kumikae.as3.panel

import java.util.concurrent.atomic.AtomicBoolean

/** The emulator exports one producer per device, shared by all open project windows. */
internal object AndroidCaptureLease {
    private data class Producer(var readers: Int, val stop: () -> Unit)
    private val producers = mutableMapOf<String, Producer>()

    private val stopped = mutableSetOf<String>()

    @Synchronized fun acquire(key: String, active: () -> Boolean, start: () -> Unit, stop: () -> Unit): AutoCloseable {
        val producer = producers.getOrPut(key) {
            // Stopping recording leaves the shared-memory name allocated in the emulator.
            val owned = stopped.remove(key) || !active()
            start()
            Producer(0) {
                if (owned) {
                    stop()
                    stopped += key
                }
            }
        }
        producer.readers++
        val closed = AtomicBoolean()
        return AutoCloseable {
            synchronized(this) {
                if (closed.compareAndSet(false, true) && --producer.readers == 0) {
                    producers.remove(key)
                    producer.stop()
                }
            }
        }
    }
}
