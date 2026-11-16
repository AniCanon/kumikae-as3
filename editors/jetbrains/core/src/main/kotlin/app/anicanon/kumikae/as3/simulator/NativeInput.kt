package app.anicanon.kumikae.as3.simulator

import java.nio.file.Files
import java.nio.file.Path

class NativeInput(
    private val executable: String,
    private val directory: Path,
    private val processes: StreamProcesses,
    private val pasteboard: GuestPasteboard,
) : InputBackend {
    @Volatile private var selected: String? = null
    private var bridge: NativeBridgeConnection? = null
    private var temporary: Path? = null
    private var streaming = false
    private var activeFeed: FrameFeed? = null

    @Volatile private var density = 1.0
    override val enabled = true
    override val continuousTouch = true
    override val notice = "Live picture and input are on through the native helper."

    @Synchronized private fun connection(device: String): NativeBridgeConnection {
        bridge?.takeIf { selected == device && it.alive && activeFeed?.ended != true }?.let { return it }
        disconnect()
        val folder = Files.createTempDirectory(directory, "native-")
        temporary = folder
        try {
            return NativeBridgeConnection.open(executable, device, folder, processes).also {
                bridge = it
                selected = device
                density = it.density
            }
        } catch (failure: Exception) {
            disconnect()
            throw failure
        }
    }

    override fun statusScale(device: String): Double = if (selected == device) density else 1.0
    override fun scale(device: String): Double = connection(device).density

    @Synchronized
    override fun frames(device: String, bind: (Geometry, Geometry) -> Long?, stopped: () -> Unit): FrameFeed {
        // A previous feed may still be reaping. Its release callback must never own the new feed.
        disconnect()
        val opened = connection(device).also { streaming = true }
        return try {
            NativeFrameFeed(device, opened, bind, stopped) {
                synchronized(this) {
                    if (bridge === opened) disconnect()
                }
            }.also {
                activeFeed = it
                it.start()
            }
        } catch (failure: Exception) {
            try {
                disconnect()
            } catch (cleanup: Exception) {
                failure.addSuppressed(cleanup)
            }
            throw failure
        }
    }

    @Synchronized override fun send(device: String, operation: String, args: List<String>) {
        val opened = if (operation in setOf("down", "move", "up")) {
            synchronized(this) {
                bridge?.takeIf { selected == device && streaming && activeFeed?.ended == false && it.alive }
                    ?: throw SimulatorException("INPUT_UNAVAILABLE", "The native picture closed; touch was cancelled")
            }
        } else {
            connection(device)
        }
        try {
            when (operation) {
                "tap" -> {
                    opened.send("down", args[0].toDouble(), args[1].toDouble())
                    opened.send("up", args[0].toDouble(), args[1].toDouble())
                }

                "down", "move", "up" -> opened.send(operation, args[0].toDouble(), args[1].toDouble())

                "button" -> opened.send(args.single().lowercase())

                "text" -> {
                    val text = args.single()
                    SimulatorText.validate(text)
                    if (SimulatorText.isAscii(text)) {
                        opened.send("text", text = text)
                    } else {
                        pasteboard.copy(device, text)
                        opened.send("paste")
                    }
                }

                "rotate" -> opened.send("rotate", orientation = args.single().lowercase())

                "swipe" -> swipe(opened, args)

                else -> throw SimulatorException("INPUT_UNAVAILABLE", "Native input does not support $operation")
            }
        } finally {
            releaseIdle()
        }
    }

    @Synchronized override fun releaseIdle() {
        if (!streaming) disconnect()
    }

    private fun swipe(opened: NativeBridgeConnection, args: List<String>) {
        val x = args[0].toDouble()
        val y = args[1].toDouble()
        val endX = args[2].toDouble()
        val endY = args[3].toDouble()
        val duration = (args[5].toDouble() * 1000).toLong()
        val steps = (duration / 17).coerceAtLeast(1).toInt()
        opened.send("down", x, y)
        val start = System.nanoTime()
        for (step in 1..steps) {
            val target = start + duration * 1_000_000 * step / steps
            val remaining = (target - System.nanoTime()) / 1_000_000
            if (remaining > 0) Thread.sleep(remaining)
            val ratio = step.toDouble() / steps
            opened.send("move", x + (endX - x) * ratio, y + (endY - y) * ratio)
        }
        opened.send("up", endX, endY)
    }

    @Synchronized private fun disconnect() {
        bridge?.awaitClosed()
        bridge = null
        selected = null
        streaming = false
        activeFeed = null
        temporary?.let {
            Files.deleteIfExists(it.resolve("frame.slot"))
            Files.deleteIfExists(it)
        }
        temporary = null
    }

    override fun close() {
        try {
            disconnect()
        } finally {
            processes.close()
        }
    }
}
