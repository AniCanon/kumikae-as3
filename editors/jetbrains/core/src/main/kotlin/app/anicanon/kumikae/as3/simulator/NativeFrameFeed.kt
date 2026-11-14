package app.anicanon.kumikae.as3.simulator

import java.util.concurrent.atomic.AtomicBoolean

internal class NativeFrameFeed(
    private val device: String,
    private val bridge: NativeBridgeConnection,
    private val bind: (Geometry, Geometry) -> Long?,
    private val stopped: () -> Unit,
    private val release: () -> Unit,
) : FrameFeed {
    private val latest = LatestValue<LiveFrame>()
    private val closed = AtomicBoolean()
    override val ended: Boolean get() = closed.get()

    @Volatile override var failure: Throwable? = null
        private set

    // Startup runs on the service worker; mapping failure must reach the fallback chooser immediately.
    private val mapped = NativeFrameReader(bridge.slot)
    private val worker = Thread({
        try {
            mapped.use { reader ->
                while (!closed.get()) {
                    check(bridge.alive) { "Native Simulator stopped. ${bridge.errors}" }
                    reader.poll()?.let { picture ->
                        val header = picture.header
                        val generation =
                            bind(
                                Geometry(header.nativeWidth, header.nativeHeight),
                                Geometry(header.width, header.height),
                            )
                        if (generation != null && !closed.get()) {
                            latest.put(
                                LiveFrame(
                                    device,
                                    generation,
                                    picture.image,
                                    header.observedNanos + bridge.clockOffsetNanos,
                                    System.nanoTime(),
                                ),
                            )
                        }
                    }
                    Thread.sleep(4)
                }
            }
        } catch (error: Exception) {
            if (!closed.get()) failure = error
        } finally {
            close()
            val interrupted = Thread.interrupted()
            try {
                try {
                    stopped()
                } finally {
                    release()
                }
            } finally {
                if (interrupted) Thread.currentThread().interrupt()
            }
        }
    }, "kumikae-native-frames").apply { isDaemon = true }

    fun start() {
        worker.start()
    }
    override fun poll(): LiveFrame? = latest.poll()

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        latest.close()
        if (Thread.currentThread() !== worker) worker.interrupt()
    }
}
