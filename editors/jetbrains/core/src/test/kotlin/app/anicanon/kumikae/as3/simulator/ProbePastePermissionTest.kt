package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import java.awt.Color
import java.awt.image.BufferedImage
import kotlin.test.assertEquals
import kotlin.test.assertNull

class ProbePastePermissionTest {
    @Test fun locatesTheLowerWideButtonOnPhoneAndTabletAndIgnoresTheAnimationBar() {
        for (width in listOf(1200, 2048)) {
            val image = BufferedImage(width, 600, BufferedImage.TYPE_INT_RGB)
            val graphics = image.createGraphics()
            try {
                graphics.color = Color.WHITE
                graphics.fillRect(0, 0, width, 600)
                assertNull(ProbePastePermission.button(image))
                graphics.color = Color(0, 122, 255)
                graphics.fillRect(width / 2 - 300, 100, 600, 40)
                graphics.fillRect(width / 2 - 60, 300, 120, 60)
                assertNull(ProbePastePermission.button(image), "The blue button refuses paste")
                graphics.color = Color(212, 212, 212)
                graphics.fillRect(width / 2 - 300, 146, 600, 40)
                assertEquals(Pixel(width / 2, 166), ProbePastePermission.button(image))
            } finally {
                graphics.dispose()
            }
        }
    }
}
