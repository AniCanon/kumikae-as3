package app.anicanon.kumikae.as3.panel

import com.sun.management.ThreadMXBean
import java.lang.management.ManagementFactory

/** Each sandbox probe opens one feed. Keep that reader's identity across both samples. */
internal class UiProbeReaderAllocation(name: String) {
    private val bean = ManagementFactory.getThreadMXBean() as? ThreadMXBean
    private val reader = Thread.getAllStackTraces().keys.singleOrNull { it.name == name }
    private fun read(): Long? {
        val thread = reader?.takeIf { it.isAlive } ?: return null
        return bean?.takeIf { it.isThreadAllocatedMemorySupported && it.isThreadAllocatedMemoryEnabled }
            ?.getThreadAllocatedBytes(thread.threadId())?.takeIf { it >= 0 }
    }

    private val initial = read()
    fun bytes(): String {
        val start = initial ?: return "unavailable"
        return read()?.takeIf { it >= start }?.let { (it - start).toString() } ?: "unavailable"
    }
}
