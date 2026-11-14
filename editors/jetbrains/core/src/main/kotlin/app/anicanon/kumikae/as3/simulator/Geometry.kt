package app.anicanon.kumikae.as3.simulator

data class Pixel(val x: Int, val y: Int)
data class Geometry(val width: Int, val height: Int) {
    init { require(width > 0 && height > 0) { "Image dimensions must be positive" } }

    fun point(x: Double, y: Double, scale: Double): Pixel {
        require(x.isFinite() && y.isFinite() && x >= 0 && y >= 0 && x < width && y < height) {
            "Coordinates are outside the screenshot"
        }
        require(scale.isFinite() && scale > 0) { "Pixels per point must be positive" }
        return Pixel((x / scale).toInt(), (y / scale).toInt())
    }

    fun fromView(x: Double, y: Double, viewWidth: Int, viewHeight: Int): Pixel? {
        if (viewWidth <= 0 || viewHeight <= 0 || !x.isFinite() || !y.isFinite()) return null
        val scale = minOf(viewWidth.toDouble() / width, viewHeight.toDouble() / height)
        val left = (viewWidth - width * scale) / 2
        val top = (viewHeight - height * scale) / 2
        val px = (x - left) / scale
        val py = (y - top) / scale
        if (px < 0 || py < 0 || px >= width || py >= height) return null
        return Pixel(px.toInt(), py.toInt())
    }
}
