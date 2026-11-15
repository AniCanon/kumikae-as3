package app.anicanon.kumikae.as3.panel

import org.junit.Test
import java.awt.image.BufferedImage
import java.lang.ref.WeakReference
import javax.swing.SwingUtilities
import kotlin.test.assertNull

class ScreenRetentionTest {
    @Test fun clearingTheScreenReleasesItsLastPresentedImage() {
        lateinit var screen: SimulatorScreen
        lateinit var picture: WeakReference<BufferedImage>
        SwingUtilities.invokeAndWait {
            screen = SimulatorScreen { _, _, _, _ -> }
            screen.setSize(200, 400)
            val image = BufferedImage(1206, 2622, BufferedImage.TYPE_INT_ARGB_PRE)
            picture = WeakReference(image)
            screen.frame = SimulatorFrame("synthetic", 1, image, 0)
            val target = BufferedImage(200, 400, BufferedImage.TYPE_INT_ARGB_PRE)
            val graphics = target.createGraphics()
            try {
                screen.paint(graphics)
            } finally {
                graphics.dispose()
            }
            screen.frame = null
        }
        // A live histogram performs a full collection, unlike sampling heap just after requesting GC.
        UiProbeHeap.collect()
        assertNull(picture.get(), "A cleared screen must release its last presented image")
        java.lang.ref.Reference.reachabilityFence(screen)
    }
}
