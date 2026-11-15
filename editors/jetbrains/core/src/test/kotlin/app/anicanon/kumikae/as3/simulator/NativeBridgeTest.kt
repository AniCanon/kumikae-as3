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

    @Test fun rotationReachesTheNativeHelper() {
        val directory = Files.createTempDirectory("native-rotate-test")
        val child = Child()
        try {
            NativeInput(
                "/bridge",
                directory,
                StreamProcesses {
                    child
                },
                GuestPasteboard { _, _ -> error("Unexpected paste") },
            ).use { input ->
                input.send("device", "rotate", listOf("LANDSCAPE_LEFT"))
                val sent = child.input.toString()
                assertTrue(sent.contains("\"op\":\"rotate\""))
                assertTrue(sent.contains("\"orientation\":\"landscape_left\""))
            }
        } finally {
            Files.delete(directory)
        }
    }

    @Test fun inputAfterFeedCloseUsesANewHelperBeforeTheOldWorkerReaps() {
        val directory = Files.createTempDirectory("native-reopen-test")
        val stopped = CountDownLatch(1)
        val release = CountDownLatch(1)
        var starts = 0
        val input = NativeInput(
            "/bridge",
            directory,
            StreamProcesses { command ->
                starts++
                RandomAccessFile(java.io.File(command.last()), "rw").use { it.setLength(NativeFrameLayout.fileBytes) }
                Child()
            },
            GuestPasteboard { _, _ -> error("Unexpected paste") },
        )
        try {
            val feed = input.frames("device", { _, _ -> 1L }, {
                stopped.countDown()
                release.await(5, TimeUnit.SECONDS)
            })
            feed.close()
            assertTrue(stopped.await(2, TimeUnit.SECONDS))
            input.send("device", "rotate", listOf("LANDSCAPE_LEFT"))
            assertEquals(2, starts)
        } finally {
            input.close()
            release.countDown()
            Files.delete(directory)
        }
    }

    @Test fun unicodeCopiesToGuestBeforeOnePasteAndAsciiStillUsesKeys() {
        val directory = Files.createTempDirectory("native-paste-test")
        val children = mutableListOf<Child>()
        val copies = mutableListOf<Pair<String, String>>()
        try {
            NativeInput(
                "/bridge",
                directory,
                StreamProcesses {
                    Child().also { children += it }
                },
                GuestPasteboard { device, text ->
                    assertFalse(children.last().input.toString().contains("paste"))
                    copies += device to text
                },
            ).use { input ->
                val unicode = "é e\u0301 日本語 👩🏽‍💻"
                input.send("device", "text", listOf(unicode))
                assertEquals(listOf("device" to unicode), copies)
                assertTrue(children[0].input.toString().contains("\"op\":\"paste\""))
                assertFalse(children[0].input.toString().contains("\"text\""))
                input.send("device", "text", listOf("aA1!"))
                assertEquals(1, copies.size)
                assertTrue(children[1].input.toString().contains("\"text\":\"aA1!\""))
            }
        } finally {
            Files.delete(directory)
        }
    }

    @Test fun pasteboardFailureSendsNoShortcut() {
        val directory = Files.createTempDirectory("native-paste-failure-test")
        val child = Child()
        try {
            NativeInput(
                "/bridge",
                directory,
                StreamProcesses { child },
                GuestPasteboard { _, _ ->
                    throw SimulatorException("PROCESS_FAILED", "Pasteboard rejected input")
                },
            ).use { input ->
                assertEquals(
                    "PROCESS_FAILED",
                    assertFailsWith<SimulatorException> {
                        input.send("device", "text", listOf("é"))
                    }.code,
                )
                assertFalse(child.input.toString().contains("paste"))
                assertTrue(child.closed)
            }
        } finally {
            Files.delete(directory)
        }
    }

    @Test fun maximumAsciiTextFitsTheNativeCommandLimit() {
        val directory = Files.createTempDirectory("native-text-limit-test")
        val child = Child()
        try {
            NativeInput(
                "/bridge",
                directory,
                StreamProcesses {
                    child
                },
                GuestPasteboard { _, _ -> error("Unexpected paste") },
            ).use { input ->
                val text = "<".repeat(4096)
                input.send("device", "text", listOf(text))
                val line = child.input.toString().lineSequence().last { it.isNotEmpty() }
                assertTrue(line.toByteArray(Charsets.UTF_8).size <= 16384)
                assertEquals(text, com.google.gson.JsonParser.parseString(line).asJsonObject["text"].asString)
            }
        } finally {
            Files.delete(directory)
        }
    }

    private class Child : StreamProcess {
        override val output = ByteArrayInputStream(
            (
                "{\"ready\":true,\"width\":2,\"height\":1,\"density\":1.0}\n" +
                    "{\"id\":1,\"ok\":true,\"receivedNanos\":100}\n" +
                    "{\"id\":2,\"ok\":true,\"receivedNanos\":200,\"completedNanos\":250}\n"
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
            assertEquals(250L, bridge.lastCompletedNanos)
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
