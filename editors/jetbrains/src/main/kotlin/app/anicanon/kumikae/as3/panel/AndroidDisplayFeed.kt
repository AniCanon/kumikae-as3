package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.NativeFrameReader
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread

internal data class AndroidPicture(val image: BufferedImage, val guestWidth: Int, val guestHeight: Int)

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
    private val latest = AtomicReference<AndroidPicture?>()
    private val inputProcess = try {
        ProcessBuilder(adb.toString(), "-s", serial, "shell").redirectOutput(ProcessBuilder.Redirect.DISCARD).start()
    } catch (failure: Exception) {
        process.destroyForcibly().waitFor()
        Files.deleteIfExists(slot)
        Files.deleteIfExists(directory)
        throw failure
    }
    private val input = inputProcess.outputStream.bufferedWriter()

    private val pointer = AndroidPointerQueue()
    private val closed = java.util.concurrent.atomic.AtomicBoolean()

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
            } catch (error: java.io.IOException) {
                if (!closed.get()) {
                    failure = error.message ?: "Android input disconnected."
                    onUpdate()
                }
            }
        }
        thread(isDaemon = true, name = "Kumikae Android display") {
            try {
                val ready = process.inputStream.bufferedReader().readLine()
                check(ready == "ready") { ready ?: "The emulator display did not start." }
                NativeFrameReader(slot) { w, h -> BufferedImage(w, h, BufferedImage.TYPE_INT_RGB) }.use { reader ->
                    while (!closed.get() && process.isAlive) {
                        reader.poll()?.let {
                            latest.set(AndroidPicture(it.image, it.header.nativeWidth, it.header.nativeHeight))
                            onUpdate()
                        }
                        Thread.sleep(4)
                    }
                    if (!closed.get()) error("The emulator display disconnected.")
                }
            } catch (error: Exception) {
                if (!closed.get()) {
                    failure = error.message ?: "The emulator display disconnected."
                    onUpdate()
                }
            } finally {
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
        latest.set(null)
        thread(isDaemon = true, name = "Kumikae Android display cleanup") {
            if (!inputProcess.waitFor(1, java.util.concurrent.TimeUnit.SECONDS)) inputProcess.destroyForcibly()
            runCatching { lease.close() }.onFailure {
                com.intellij.openapi.diagnostic.Logger.getInstance(
                    AndroidDisplayFeed::class.java,
                ).warn("Releasing emulator capture", it)
            }
        }
    }
}
