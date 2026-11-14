package app.anicanon.kumikae.as3.panel

import java.awt.image.BufferedImage
import javax.swing.JComponent

/** Answers for the person, and counts what the panel copied. No dialog or popup is opened. */
internal class UiProbePrompts : SimulatorPrompts {
    var copiedImages = 0
        private set

    override fun appToInstall(): String = "/probe/Missing.app"

    override fun textToType(): String? = null

    override fun confirmInput() = true

    override fun confirmInstall() = true

    override fun choose(under: JComponent, entries: List<MenuEntry>) = Unit

    override fun copy(text: String) = Unit

    override fun copy(image: BufferedImage) {
        copiedImages++
    }
}
