package app.anicanon.kumikae.as3.panel

import org.junit.Test
import java.awt.RenderingHints
import java.awt.image.BufferedImage
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNotSame
import kotlin.test.assertNull
import kotlin.test.assertSame

class DeviceBodyCacheTest {
    @Test fun oversizedRetinaBodyUsesDirectPainting() {
        val cache = DeviceBodyCache()
        assertNull(cache.image(DeviceBodySize(1206, 2622, 1200, 1200, 18, 2.0, 2.0)))
        assertNull(cache.image(DeviceBodySize(1206, 2622, 4800, 7200, 18, 2.0, 2.0)))
    }

    @Test fun retinaBodyMatchesDirectPaintingAndSurvivesScreenOnlyUpdates() {
        val size = DeviceBodySize(1206, 2622, 200, 400, 18, 2.0, 2.0)
        val cache = DeviceBodyCache()
        val image = assertNotNull(cache.image(size))
        val reference = BufferedImage(400, 800, BufferedImage.TYPE_INT_ARGB_PRE)
        val canvas = reference.createGraphics()
        try {
            canvas.scale(2.0, 2.0)
            canvas.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
            canvas.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_QUALITY)
            DeviceFrame(1206, 2622, 200, 400, 18).paintBody(canvas)
        } finally {
            canvas.dispose()
        }
        for (y in 0 until image.height step 3) {
            for (x in 0 until image.width step 3) assertEquals(reference.getRGB(x, y), image.getRGB(x, y))
        }
        assertSame(image, cache.image(size))
        assertNotSame(image, cache.image(size.copy(screenWidth = 2622, screenHeight = 1206)))
        assertNotSame(cache.image(size), cache.image(size.copy(scaleX = 1.0, scaleY = 1.0)))
    }
}
