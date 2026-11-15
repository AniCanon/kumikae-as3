package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.command.impl.UndoManagerImpl
import com.intellij.openapi.command.undo.UndoManager
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.fileEditor.TextEditor
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.DialogWrapper
import java.awt.Component
import java.awt.Container
import java.awt.Dialog
import java.awt.Window
import javax.swing.JLabel
import javax.swing.Timer
import javax.swing.text.JTextComponent

/** An unexpected Undo dialog fails the probe instead of blocking its entire JVM until timeout. */
internal object UiProbeUndo {
    fun undo(project: Project, editor: Editor) {
        val file = checkNotNull(FileDocumentManager.getInstance().getFile(editor.document))
        val target = FileEditorManager.getInstance(project).getEditors(file).filterIsInstance<TextEditor>()
            .single { it.editor === editor }
        val manager = UndoManager.getInstance(project)
        val state = if (manager is UndoManagerImpl && manager.isNextUndoAskConfirmation(target)) {
            manager.dumpState(target, "Before completion Undo")
        } else {
            ""
        }
        var failure: String? = null
        val timer = Timer(25) {
            Window.getWindows().filterIsInstance<Dialog>().filter { it.isShowing && it.title == "Undo" }
                .forEach { window ->
                    val dialog = DialogWrapper.findInstance(window)
                    failure =
                        "Undo displayed ${dialog?.javaClass?.name ?: window.javaClass.name}: ${text(window)}\n$state"
                    if (dialog != null) dialog.close(DialogWrapper.CANCEL_EXIT_CODE) else window.dispose()
                }
        }
        timer.start()
        try {
            manager.undo(target)
        } finally {
            timer.stop()
        }
        check(failure == null) { checkNotNull(failure) }
    }

    private fun text(component: Component): String = when (component) {
        is JLabel -> component.text.orEmpty()
        is JTextComponent -> component.text.orEmpty()
        is Container -> component.components.joinToString(" ", transform = ::text)
        else -> ""
    }
}
