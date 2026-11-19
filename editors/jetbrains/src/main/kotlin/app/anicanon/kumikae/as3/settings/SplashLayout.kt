package app.anicanon.kumikae.as3.settings

import java.awt.Color
import java.awt.Rectangle
import kotlin.math.roundToInt

/** Godot's boot image scales against the window before the scene's stretch policy takes effect. */
internal data class SplashLayout(
    val width: Int,
    val height: Int,
    val mode: Int,
    val background: Color = Color(36, 36, 36),
    val visible: Boolean = true,
    val filter: Boolean = true,
) {
    fun placement(imageWidth: Int, imageHeight: Int): Rectangle {
        if (width <= 0 || height <= 0 || imageWidth <= 0 || imageHeight <= 0) return Rectangle(0, 0, 0, 0)
        val sx = width.toDouble() / imageWidth
        val sy = height.toDouble() / imageHeight
        val scale = when (mode) {
            0 -> 1.0
            2 -> sx
            3 -> sy
            4 -> maxOf(sx, sy)
            5 -> return Rectangle(0, 0, width, height)
            else -> if (width > height) sy else sx
        }
        val w = (imageWidth * scale).roundToInt()
        val h = (imageHeight * scale).roundToInt()
        return Rectangle((width - w) / 2, (height - h) / 2, w, h)
    }

    companion object {
        fun read(settings: ProjectSettings): SplashLayout {
            val config = GodotConfig(settings.projectText)
            fun raw(key: String) = config.raw("application", "boot_splash/$key")
            fun windowSize(axis: String, fallback: String): Int =
                config.raw("display", "window/size/window_${axis}_override")
                    ?.toIntOrNull()?.takeIf { it > 0 } ?: fallback.toIntOrNull()?.coerceAtLeast(1) ?: 1
            val channels = raw("bg_color")?.removePrefix("Color(")?.removeSuffix(")")
                ?.split(',')?.map { it.trim().toFloatOrNull()?.takeIf { channel -> channel in 0f..1f } }
            val color = if (channels?.size == 4 && channels.all { it != null }) {
                Color(channels[0]!!, channels[1]!!, channels[2]!!, channels[3]!!)
            } else {
                Color(36, 36, 36)
            }
            return SplashLayout(
                windowSize("width", settings.width),
                windowSize("height", settings.height),
                raw("stretch_mode")?.toIntOrNull() ?: 1,
                color,
                raw("show_image") != "false",
                raw("use_filter") != "false",
            )
        }
    }
}
