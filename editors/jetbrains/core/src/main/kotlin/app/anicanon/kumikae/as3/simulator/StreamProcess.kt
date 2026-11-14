package app.anicanon.kumikae.as3.simulator

import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

interface StreamProcess : AutoCloseable {
    val output: InputStream
    val input: OutputStream
    val errors: String
    val alive: Boolean get() = true
}

fun interface StreamProcesses : AutoCloseable {
    fun start(command: List<String>): StreamProcess
    override fun close() = Unit
}

/** Session shutdown awaits every owned child, including a video whose nonblocking close is still reaping. */
class OwnedStreamProcesses : StreamProcesses {
    private val children = mutableListOf<NativeStreamProcess>()
    private var closed = false
    private var shutdown: Thread? = null

    @Synchronized override fun start(command: List<String>): StreamProcess {
        check(!closed) { "Process owner is closed" }
        if (shutdown == null) {
            shutdown = Thread({ close() }, "kumikae-idb-shutdown").also {
                Runtime.getRuntime().addShutdownHook(it)
            }
        }
        children.removeAll { it.terminated }
        return NativeStreamProcess(command).also { children += it }
    }

    @Synchronized override fun close() {
        if (closed) return
        closed = true
        try {
            children.forEach { it.close() }
            children.forEach { it.awaitClosed() }
            children.clear()
        } finally {
            val hook = shutdown
            shutdown = null
            if (hook != null && Thread.currentThread() !== hook) {
                try {
                    Runtime.getRuntime().removeShutdownHook(hook)
                } catch (_: IllegalStateException) {
                    // JVM shutdown already started; its hook will observe the closed owner.
                }
            }
        }
    }
}

/** Owns only the child it starts. Closing is nonblocking; its reaper waits and closes the pipes. */
class NativeStreamProcess(command: List<String>) : StreamProcess {
    private val process = ProcessBuilder(command).start()
    private val stopped = AtomicBoolean()
    private val reaped = CountDownLatch(1)
    val terminated: Boolean get() = reaped.count == 0L
    override val alive: Boolean get() = process.isAlive
    private val errorLock = Any()
    private var tail = ""
    override val output: InputStream = process.inputStream
    override val input: OutputStream = process.outputStream
    override val errors: String get() = synchronized(errorLock) { tail }

    init {
        Thread({
            runCatching {
                process.errorStream.reader().use { reader ->
                    val buffer = CharArray(2048)
                    while (true) {
                        val count = reader.read(buffer)
                        if (count < 0) break
                        synchronized(errorLock) { tail = (tail + String(buffer, 0, count)).takeLast(4096) }
                    }
                }
            }
        }, "kumikae-idb-errors").apply { isDaemon = true }.start()
    }

    override fun close() {
        if (!stopped.compareAndSet(false, true)) return
        val children = process.descendants().toList()
        children.forEach { it.destroy() }
        process.destroy()
        Thread({
            try {
                if (!process.waitFor(1, TimeUnit.SECONDS)) process.destroyForcibly()
                children.forEach { if (it.isAlive) it.destroyForcibly() }
                process.waitFor(1, TimeUnit.SECONDS)
                runCatching { input.close() }
                runCatching { output.close() }
                runCatching { process.errorStream.close() }
            } finally {
                reaped.countDown()
            }
        }, "kumikae-idb-reaper").apply { isDaemon = true }.start()
    }

    fun awaitClosed() {
        close()
        check(reaped.await(3, TimeUnit.SECONDS) && !process.isAlive) { "idb child did not stop" }
    }
}

/** Bounded replies and a deadline keep a broken companion from hanging input indefinitely. */
internal class ProcessLines(private val child: StreamProcess) : AutoCloseable {
    val alive: Boolean get() = child.alive
    private val replies = ArrayBlockingQueue<Result<String>>(16)
    private val reader = Thread({
        try {
            child.output.bufferedReader().use { input ->
                while (true) {
                    val line = StringBuilder()
                    while (true) {
                        val next = input.read()
                        if (next < 0) error("idb closed its output; input completion is unknown")
                        if (next == 10) break
                        check(line.length < 65536) { "idb reply is too large" }
                        line.append(next.toChar())
                    }
                    replies.put(Result.success(line.toString()))
                }
            }
        } catch (failure: Exception) {
            replies.offer(Result.failure(failure))
        }
    }, "kumikae-idb-replies").apply {
        isDaemon = true
        start()
    }

    fun next(): String = try {
        (replies.poll(15, TimeUnit.SECONDS) ?: error("idb reply timed out; input completion is unknown")).getOrThrow()
    } catch (failure: Exception) {
        close()
        throw SimulatorException("INPUT_UNAVAILABLE", "${failure.message}. ${child.errors}".take(4096))
    }

    override fun close() {
        child.close()
        reader.interrupt()
    }
}
