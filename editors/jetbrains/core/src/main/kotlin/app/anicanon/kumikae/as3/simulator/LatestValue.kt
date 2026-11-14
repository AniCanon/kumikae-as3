package app.anicanon.kumikae.as3.simulator

/** A single replaceable value: slow consumers never build a queue. */
class LatestValue<T : Any> : AutoCloseable {
    private val lock = Object()
    private var value: T? = null
    private var closed = false

    fun put(next: T) = synchronized(lock) {
        if (!closed) {
            value = next
            lock.notifyAll()
        }
    }

    fun poll(): T? = synchronized(lock) { value.also { value = null } }

    fun take(): T? = synchronized(lock) {
        while (value == null && !closed) lock.wait()
        value.also { value = null }
    }

    override fun close() = synchronized(lock) {
        closed = true
        value = null
        lock.notifyAll()
    }
}
