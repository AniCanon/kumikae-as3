package app.anicanon.kumikae.as3.panel

import com.sun.management.GarbageCollectorMXBean
import java.lang.management.ManagementFactory
import java.lang.management.MemoryType
import javax.management.ObjectName

/** Operates only on this JVM. With no -all option, the live histogram forces a full collection. */
internal object UiProbeHeap {
    fun collect(): String = ManagementFactory.getPlatformMBeanServer().invoke(
        ObjectName("com.sun.management:type=DiagnosticCommand"),
        "gcClassHistogram",
        arrayOf(emptyArray<String>()),
        arrayOf("[Ljava.lang.String;"),
    ) as String

    fun usedMiB(): Double {
        val pools = ManagementFactory.getMemoryPoolMXBeans().filter { it.type == MemoryType.HEAP }.map { it.name }
        val collection = ManagementFactory.getGarbageCollectorMXBeans()
            .mapNotNull { (it as? GarbageCollectorMXBean)?.lastGcInfo }.maxBy { it.endTime }
        return collection.memoryUsageAfterGc.filterKeys { it in pools }.values.sumOf { it.used } / 1048576.0
    }
}
