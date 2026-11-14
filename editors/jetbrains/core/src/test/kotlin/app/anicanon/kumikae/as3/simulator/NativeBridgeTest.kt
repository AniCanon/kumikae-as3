package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.RandomAccessFile
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class NativeBridgeTest {
    @Test fun closingAFeedNeverCallsSessionCodeOnTheCaller() {
        val directory = Files.createTempDirectory("native-close-test")
        val child = Child()
        val stopped = CountDownLatch(1)
        val released = CountDownLatch(1)
        val caller = Thread.currentThread()
        var stoppedThread: Thread? = null
        try {
            val bridge = NativeBridgeConnection.open(
                "/bridge",
                "device",
                directory,
                StreamProcesses {
                    RandomAccessFile(directory.resolve("frame.slot").toFile(), "rw").use {
                        it.setLength(NativeFrameLayout.fileBytes)
                    }
                    child
                },
            )
            val feed = NativeFrameFeed("device", bridge, { _, _ -> 1L }, {
                stoppedThread = Thread.currentThread()
                stopped.countDown()
            }, {
                bridge.awaitClosed()
                released.countDown()
            })
            // Closing before the worker starts also exercises the late-start disposal path.
            feed.close()
            feed.start()
            assertTrue(stopped.await(2, TimeUnit.SECONDS))
            assertFalse(stoppedThread === caller)
            assertTrue(released.await(2, TimeUnit.SECONDS))
        } finally {
            Files.deleteIfExists(directory.resolve("frame.slot"))
            Files.delete(directory)
        }
    }

    @Test fun preservesTransientDeviceFailureAndReapsTheFailedHelper() {
        val directory = Files.createTempDirectory("native-bridge-error-test")
        var closed = false
        val child = object : StreamProcess {
            override val output = ByteArrayInputStream(
                "{\"error\":\"Simulator is stopped\",\"code\":\"DEVICE_UNAVAILABLE\"}\n".toByteArray(),
            )
            override val input = ByteArrayOutputStream()
            override val errors = ""
            override fun close() {
                closed = true
            }
        }
        try {
            val failure = assertFailsWith<SimulatorException> {
                NativeBridgeConnection.open("/bridge", "device", directory, StreamProcesses { child })
            }
            assertEquals("DEVICE_UNAVAILABLE", failure.code)
            assertTrue(closed)
        } finally {
            Files.delete(directory)
        }
    }

    private class Child : StreamProcess {
        override val output = ByteArrayInputStream(
            (
                "{\"ready\":true,\"width\":2,\"height\":1,\"density\":1.0}\n" +
                    "{\"id\":1,\"ok\":true,\"receivedNanos\":100}\n" +
                    "{\"id\":2,\"ok\":true,\"receivedNanos\":200}\n"
                ).toByteArray(),
        )
        override val input = ByteArrayOutputStream()
        override val errors = ""
        var closed = false
        override fun close() {
            closed = true
        }
    }

    @Test fun ownsHelperAndUsesBinarySlot() {
        val directory = Files.createTempDirectory("native-bridge-test")
        val child = Child()
        var command = emptyList<String>()
        try {
            val bridge = NativeBridgeConnection.open(
                "/bridge",
                "device",
                directory,
                StreamProcesses {
                    command = it
                    Files.write(directory.resolve("frame.slot"), byteArrayOf())
                    child
                },
            )
            assertEquals(
                listOf("/bridge", "--device", "device", "--frames", directory.resolve("frame.slot").toString()),
                command,
            )
            assertEquals(1.0, bridge.density)
            bridge.send("home")
            assertTrue(child.input.toString().contains("\"op\":\"home\""))
            assertFalse(child.closed)
            bridge.close()
            assertTrue(child.closed)
        } finally {
            Files.deleteIfExists(directory.resolve("frame.slot"))
            Files.deleteIfExists(directory)
        }
    }
}
