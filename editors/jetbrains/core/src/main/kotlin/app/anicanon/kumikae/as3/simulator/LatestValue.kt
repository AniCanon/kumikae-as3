package app.anicanon.kumikae.as3.simulator

import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/** A single replaceable value: slow consumers never build a queue. */
class LatestValue<T : Any> : AutoCloseable {
    private val lock = ReentrantLock()
    private val changed = lock.newCondition()
    private var value: T? = null
    private var closed = false

    fun put(next: T) = lock.withLock {
        if (!closed) {
            value = next
            changed.signalAll()
        }
    }

    fun poll(): T? = lock.withLock { value.also { value = null } }

    fun take(): T? = lock.withLock {
        while (value == null && !closed) changed.await()
        value.also { value = null }
    }

    override fun close() = lock.withLock {
        closed = true
        value = null
        changed.signalAll()
    }
}
