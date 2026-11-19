package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.FrameImagePool
import app.anicanon.kumikae.as3.simulator.NativeFrameReader
import com.intellij.openapi.diagnostic.Logger
import java.awt.image.BufferedImage
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread

internal data class AndroidPicture(
    val image: BufferedImage,
    val guestWidth: Int,
    val guestHeight: Int,
    val sequence: Long,
    val observedAt: Long,
    val decodedAt: Long,
)

internal class AndroidDisplayFeed(
    helper: Path,
    handle: String,
    adb: Path,
    serial: String,
    edge: Int,
    private val lease: AutoCloseable,
) : AutoCloseable {
    private val directory = Files.createTempDirectory("kumikae-android-display-")
    private val slot = directory.resolve("frames")
    private val process = try {
        ProcessBuilder(helper.toString(), "--android-display", handle, edge.toString(), slot.toString())
            .redirectErrorStream(true).start()
    } catch (error: Exception) {
        Files.deleteIfExists(directory)
        throw error
    }
    private val images = FrameImagePool(BufferedImage.TYPE_INT_RGB)
    fun retire(previous: AndroidPicture) = images.release(previous.image)
    private val latest = AtomicReference<AndroidPicture?>()
    private val inputProcess = try {
        ProcessBuilder(adb.toString(), "-s", serial, "shell")
            .redirectErrorStream(true)
            .redirectOutput(ProcessBuilder.Redirect.DISCARD)
            .start()
    } catch (failure: Exception) {
        process.destroyForcibly().waitFor()
        Files.deleteIfExists(slot)
        Files.deleteIfExists(directory)
        throw failure
    }
    private val input = try {
        inputProcess.outputStream.bufferedWriter()
    } catch (failure: Exception) {
        inputProcess.destroyForcibly().waitFor()
        process.destroyForcibly().waitFor()
        Files.deleteIfExists(slot)
        Files.deleteIfExists(directory)
        throw failure
    }

    private val pointer = AndroidPointerQueue()
    private val closed = AtomicBoolean()
    private val displayThread: Thread

    @Volatile var onUpdate: () -> Unit = {}

    @Volatile var failure: String? = null
        private set

    init {
        thread(isDaemon = true, name = "Kumikae Android input") {
            try {
                input.use { writer ->
                    while (true) {
                        val motion = pointer.take()
                        if (motion.isEmpty()) break
                        writer.write("cmd input motionevent $motion\n")
                        writer.flush()
                    }
                }
            } catch (error: IOException) {
                if (!closed.get()) {
                    failure = error.message ?: "Android input disconnected."
                    onUpdate()
                }
            }
        }
        displayThread = thread(isDaemon = true, name = "Kumikae Android display") {
            try {
                val output = process.inputStream.bufferedReader()
                val ready = output.readLine()
                check(ready == "ready") { ready ?: "The emulator display did not start." }
                thread(isDaemon = true, name = "Kumikae Android display output") {
                    runCatching { output.use { reader -> while (reader.readLine() != null) {} } }
                }
                NativeFrameReader(slot, discard = images::release, allocate = images::acquire).use { reader ->
                    while (!closed.get() && process.isAlive) {
                        reader.poll()?.let {
                            val picture = AndroidPicture(
                                it.image,
                                it.header.nativeWidth,
                                it.header.nativeHeight,
                                it.header.sequence,
                                it.header.observedNanos,
                                System.nanoTime(),
                            )
                            latest.getAndSet(picture)?.let(::retire)
                            onUpdate()
                        }
                        Thread.sleep(4)
                    }
                    if (!closed.get()) error("The emulator display disconnected.")
                }
            } catch (_: InterruptedException) {
                // Closing interrupts the polling sleep.
            } catch (error: Exception) {
                if (!closed.get()) {
                    failure = error.message ?: "The emulator display disconnected."
                    onUpdate()
                }
            } finally {
                process.destroy()
                if (!process.waitFor(2, TimeUnit.SECONDS)) process.destroyForcibly().waitFor()
                Files.deleteIfExists(slot)
                Files.deleteIfExists(directory)
            }
        }
    }

    fun poll() = latest.getAndSet(null)
    fun touch(phase: String, x: Int, y: Int) = pointer.offer(phase, x, y)
    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        onUpdate = {}
        pointer.close()
        process.destroy()
        displayThread.interrupt()
        latest.getAndSet(null)?.let(::retire)
        thread(isDaemon = true, name = "Kumikae Android display cleanup") {
            if (!inputProcess.waitFor(1, TimeUnit.SECONDS)) inputProcess.destroyForcibly()
            runCatching { lease.close() }.onFailure {
                Logger.getInstance(AndroidDisplayFeed::class.java).warn("Releasing emulator capture", it)
            }
        }
    }
}
