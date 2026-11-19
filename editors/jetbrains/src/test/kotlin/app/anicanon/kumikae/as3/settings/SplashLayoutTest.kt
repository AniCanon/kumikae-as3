package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import java.awt.Rectangle
import java.awt.image.BufferedImage
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SplashLayoutTest {
    @Test fun `window overrides and translucent background match the launch settings`() {
        val project = """
            [display]
            window/size/viewport_width=960
            window/size/viewport_height=720
            window/size/window_width_override=1200
            window/size/window_height_override=0
            [application]
            boot_splash/bg_color=Color(0, 0, 0, 0.25)
            boot_splash/stretch_mode=2
            boot_splash/use_filter=false
        """.trimIndent()
        val layout = SplashLayout.read(ProjectSettings.read(project, ""))
        assertEquals(Rectangle(0, 22, 1200, 675), layout.placement(1920, 1080))
        assertEquals(64, layout.background.alpha)
        assertEquals(false, layout.filter)
    }

    @Test fun `tree icons fit rectangular artwork and unfiltered thumbnails keep their palette`() {
        val image = BufferedImage(1600, 800, BufferedImage.TYPE_INT_RGB)
        for (x in 0 until image.width step 2) {
            for (y in 0 until image.height) image.setRGB(x, y, -1)
        }
        val art = GameArtwork.bounded(Artwork(image), false)
        assertEquals(960, art.image.width)
        assertEquals(480, art.image.height)
        assertEquals(1600, art.width)
        assertTrue((0 until 960).all { art.image.getRGB(it, 0) in setOf(-1, -16777216) })
        val icon = art.treeIcon
        val result = BufferedImage(16, 16, BufferedImage.TYPE_INT_ARGB)
        val g = result.createGraphics()
        try {
            icon.paintIcon(null, g, 0, 0)
        } finally {
            g.dispose()
        }
        assertEquals(0, result.getRGB(8, 3))
        assertEquals(255, result.getRGB(8, 4) ushr 24)
        assertEquals(0, result.getRGB(8, 12))
    }

    @Test fun `an ultrawide title preserves the configured Godot stretch mode in a four by three viewport`() {
        val expected = listOf(
            Rectangle(-480, -180, 1920, 1080),
            Rectangle(-160, 0, 1280, 720),
            Rectangle(0, 90, 960, 540),
            Rectangle(-160, 0, 1280, 720),
            Rectangle(-160, 0, 1280, 720),
            Rectangle(0, 0, 960, 720),
        )
        for ((mode, rect) in expected.withIndex()) {
            assertEquals(rect, SplashLayout(width = 960, height = 720, mode = mode).placement(1920, 1080))
        }
        assertEquals(Rectangle(0, 330, 640, 360), SplashLayout(640, 1020, 1).placement(1920, 1080))
    }
}
