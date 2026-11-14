package app.anicanon.kumikae.as3.simulator

/** One native geometry and its two presentations: an agent snapshot and a scaled live picture. */
class FrameGeometry {
    var generation = 0L
        private set
    private var native: Geometry? = null
    private var live: Geometry? = null

    fun record(size: Geometry, displayed: Geometry = size): Long {
        if (native != size) {
            invalidate()
            native = size
        }
        if (displayed != size) live = displayed
        return generation
    }

    fun point(stamp: Long, displayed: Geometry, x: Double, y: Double, density: Double): Pixel {
        val size = native
        if (stamp != generation || size == null || (displayed != size && displayed != live)) {
            throw SimulatorException("STALE_FRAME", "Screen geometry changed; take a new screenshot")
        }
        displayed.point(x, y, 1.0)
        return size.point(x * size.width / displayed.width, y * size.height / displayed.height, density)
    }

    fun invalidate() {
        generation++
        native = null
        live = null
    }
}
