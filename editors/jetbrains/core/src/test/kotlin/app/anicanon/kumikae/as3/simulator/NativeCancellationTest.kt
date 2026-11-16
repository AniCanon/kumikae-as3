package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.io.SequenceInputStream
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class NativeCancellationTest {
    @Test fun interruptionWhileWaitingForReadyOrPingClosesTheConnection() {
        for (ready in listOf(false, true)) {
            val directory = Files.createTempDirectory("native-cancel-test")
            val waiting = CountDownLatch(1)
            val pipe = object : PipedInputStream() {
                override fun read(bytes: ByteArray, offset: Int, length: Int): Int {
                    if (!ready) waiting.countDown()
                    return super.read(bytes, offset, length)
                }
            }
            val writer = PipedOutputStream(pipe)
            val prefix = if (ready) "{\"ready\":true,\"width\":2,\"height\":1,\"density\":1}\n" else ""
            val child = object : StreamProcess {
                override val output = SequenceInputStream(ByteArrayInputStream(prefix.toByteArray()), pipe)
                override val input = object : ByteArrayOutputStream() {
                    override fun flush() {
                        if (ready) waiting.countDown()
                    }
                }
                override val errors = ""
                override fun close() {
                    writer.close()
                    pipe.close()
                }
            }
            val native = object : InputBackend {
                override val enabled = true
                override fun scale(device: String): Double =
                    NativeBridgeConnection.open("/bridge", device, directory, StreamProcesses { child }).density
                override fun send(device: String, operation: String, args: List<String>) = Unit
            }
            val failure = AtomicReference<Throwable?>()
            val worker = Thread {
                try {
                    native.scale("device")
                } catch (error: Throwable) {
                    failure.set(error)
                }
            }
            try {
                worker.start()
                assertTrue(waiting.await(2, TimeUnit.SECONDS))
                worker.interrupt()
                worker.join(2000)
                assertFalse(worker.isAlive)
                assertIs<InterruptedException>(failure.get())
            } finally {
                child.close()
                worker.interrupt()
                worker.join(2000)
                native.close()
                Files.delete(directory)
            }
        }
    }
}
