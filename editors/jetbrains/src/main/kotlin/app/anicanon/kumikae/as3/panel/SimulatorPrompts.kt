package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptor
import com.intellij.openapi.ide.CopyPasteManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.ui.popup.JBPopupFactory
import com.intellij.openapi.ui.popup.PopupStep
import com.intellij.openapi.ui.popup.util.BaseListPopupStep
import java.awt.Image
import java.awt.Toolkit
import java.awt.datatransfer.DataFlavor
import java.awt.datatransfer.StringSelection
import java.awt.datatransfer.Transferable
import java.awt.datatransfer.UnsupportedFlavorException
import java.awt.image.BufferedImage
import javax.swing.JComponent

/** One line of a menu the panel opens. */
internal class MenuEntry(val text: String, val enabled: Boolean = true, val action: () -> Unit)

/** What the panel asks of the person and the IDE around it. The view never opens a dialog itself. */
internal interface SimulatorPrompts {
    fun appToInstall(): String?

    fun textToType(): String?

    /** The owner's yes to private Simulator interfaces. */
    fun confirmInput(): Boolean

    /** The owner's yes to what the install puts on this Mac. */
    fun confirmInstall(): Boolean

    fun choose(under: JComponent, entries: List<MenuEntry>)

    fun copy(text: String)

    fun copy(image: BufferedImage)
}

internal class IdePrompts(private val project: Project) : SimulatorPrompts {
    override fun appToInstall(): String? {
        // An .app is a folder; the chooser must offer folders and stop at one.
        val descriptor = FileChooserDescriptor(false, true, false, false, false, false)
            .withTitle("Install App")
            .withDescription("Choose an .app built for the Simulator.")
        return FileChooser.chooseFile(descriptor, project, null)?.path
    }

    override fun textToType(): String? =
        Messages.showInputDialog(project, "Text to type into the Simulator:", "Type Text", null)?.takeIf {
            it.isNotEmpty()
        }

    override fun confirmInput(): Boolean = Messages.showYesNoDialog(
        project,
        "Live pictures and controls use the bundled native helper through private Apple interfaces. " +
            "Apple can change those without notice.\n\n" +
            "If the helper is unavailable, an installed idb is used, then public screenshots. " +
            "Nothing is installed by this.",
        "Switch Simulator Input On?",
        "Switch On",
        "Cancel",
        Messages.getQuestionIcon(),
    ) == Messages.YES

    override fun confirmInstall(): Boolean = Messages.showYesNoDialog(
        project,
        "This installs idb, Meta's tool for driving the Simulator, on this Mac:\n\n" +
            "• idb-companion, with Homebrew, from Meta's tap (facebook/fb). " +
            "Homebrew is told to trust that one formula.\n" +
            "• the fb-idb client, in its own Python environment, as ~/.local/bin/idb.\n\n" +
            "It needs Homebrew and can take a few minutes. What it prints is shown in the console.",
        "Install idb?",
        "Install",
        "Cancel",
        Messages.getQuestionIcon(),
    ) == Messages.YES

    override fun choose(under: JComponent, entries: List<MenuEntry>) {
        val step = object : BaseListPopupStep<MenuEntry>(null, entries) {
            override fun getTextFor(value: MenuEntry) = value.text

            override fun isSelectable(value: MenuEntry) = value.enabled

            override fun onChosen(selectedValue: MenuEntry, finalChoice: Boolean): PopupStep<*>? =
                doFinalStep { selectedValue.action() }
        }
        JBPopupFactory.getInstance().createListPopup(step).showUnderneathOf(under)
    }

    override fun copy(text: String) = CopyPasteManager.getInstance().setContents(StringSelection(text))

    // Not the IDE's clipboard manager: its history keeps text only, and it throws on a picture.
    override fun copy(image: BufferedImage) = Toolkit.getDefaultToolkit().systemClipboard.setContents(
        object : Transferable {
            override fun getTransferDataFlavors() = arrayOf(DataFlavor.imageFlavor)

            override fun isDataFlavorSupported(flavor: DataFlavor) = flavor == DataFlavor.imageFlavor

            override fun getTransferData(flavor: DataFlavor): Image {
                if (flavor != DataFlavor.imageFlavor) throw UnsupportedFlavorException(flavor)
                return image
            }
        },
        null,
    )
}
