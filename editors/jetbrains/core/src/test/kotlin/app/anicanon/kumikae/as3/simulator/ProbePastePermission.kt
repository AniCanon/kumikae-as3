package app.anicanon.kumikae.as3.simulator

import java.awt.image.BufferedImage

/** On the measured runtime, gray Allow Paste sits below blue Don't Allow Paste. Owned fixture only. */
internal object ProbePastePermission {
    fun button(image: BufferedImage): Pixel? {
        val center = image.width / 2
        var top: Int? = null
        fun blue(x: Int, y: Int): Boolean {
            val color = image.getRGB(x, y)
            return (color and 255) > 220 && ((color shr 16) and 255) < 30
        }
        fun gray(x: Int, y: Int): Boolean {
            val color = image.getRGB(x, y)
            val red = (color shr 16) and 255
            val green = (color shr 8) and 255
            val blue = color and 255
            return red in 180..230 && kotlin.math.abs(red - green) < 5 && kotlin.math.abs(red - blue) < 5
        }
        val left = center - image.width / 10
        val right = center + image.width / 10
        for (y in 0..image.height) {
            // Off-center samples avoid the button's label and reject the narrow animated bar.
            val wide = y < image.height && blue(left, y) && blue(right, y)
            if (wide && top == null) top = y
            if (!wide) {
                top?.let {
                    val height = y - it
                    val allowY = y + height * 2 / 3
                    if (height >= 8 && allowY < image.height && gray(left, allowY) && gray(right, allowY)) {
                        return Pixel(center, allowY)
                    }
                }
                top = null
            }
        }
        return null
    }
}
