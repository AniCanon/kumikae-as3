package app.anicanon.kumikae.as3.panel

import com.intellij.lang.documentation.ide.IdeDocumentationTargetProvider
import com.intellij.openapi.actionSystem.IdeActions
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.ScrollType
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.popup.util.PopupUtil
import com.intellij.openapi.wm.FocusWatcher
import com.intellij.openapi.wm.ex.WindowManagerEx
import com.intellij.psi.PsiDocumentManager
import java.awt.Component
import java.awt.Container
import java.awt.Window
import java.awt.event.FocusEvent
import javax.swing.JEditorPane

/**
 * The server's hover, as the editor renders and shows it. A mouse hover is not used: the platform shows
 * none while another application has the desktop's focus, and the probe runs in the background. Quick
 * documentation asks the same documentation targets and renders the same reply, code block included.
 */
internal object UiProbeHover {
    private const val SIGNATURE = "function move(amount:int):void"
    private const val CONTEXT = "method of CompletionProbe"

    /** The server answers with the signature in a code block marked with the plugin's language, then its owner. */
    fun run(project: Project, editor: Editor, observations: MutableList<String>) {
        lateinit var source: String
        UiProbeCompletion.edt { source = editor.document.text }
        val call = source.lastIndexOf("move(")
        check(call > source.indexOf("move(")) { "Expected both a method declaration and its call" }
        show(project, editor, call + 1)
        UiProbeCompletion.await("hover with the server's signature") {
            shown().any { it.contains(SIGNATURE) && it.contains(CONTEXT) }
        }
        close(SIGNATURE)
        observations += "Hover documentation shows the server's signature and its owner"
    }

    /** Without a client nothing answers, and asking is still harmless. */
    fun checkAbsent(project: Project, editor: Editor, observations: MutableList<String>) {
        lateinit var source: String
        UiProbeCompletion.edt { source = editor.document.text }
        val symbol = source.indexOf("transform") + 1
        UiProbeCompletion.edt {
            val file = checkNotNull(PsiDocumentManager.getInstance(project).getPsiFile(editor.document))
            val targets = IdeDocumentationTargetProvider.getInstance(project).documentationTargets(editor, file, symbol)
            check(targets.isEmpty()) { "A documentation target answered without a client: $targets" }
        }
        show(project, editor, symbol)
        // Absence has no event to wait for: the request is local and has no target, so a short bound is enough.
        Thread.sleep(ABSENCE_MILLIS)
        UiProbeCompletion.edt {
            check(shown().none { it.contains("CompletionProbe") || it.contains("Node2D") }) {
                "Documentation showed server text without a client: ${shown()}"
            }
        }
        close("CompletionProbe")
        observations += "Without a client, no documentation target answers and none is shown"
    }

    private fun show(project: Project, editor: Editor, offset: Int) {
        UiProbeCompletion.edt {
            editor.caretModel.moveToOffset(offset)
            editor.scrollingModel.scrollToCaret(ScrollType.MAKE_VISIBLE)
        }
        UiProbeCompletion.await("visible documentation target") {
            editor.scrollingModel.visibleArea.contains(editor.offsetToXY(offset))
        }
        // Quick documentation casts the window's focused component, and the platform's answer is the window
        // itself while it is the active window and no component of it has been given the focus: so it is in
        // a sandbox started behind another application. The window's focus watcher is told what the desktop
        // would have told it had the sandbox been in front.
        UiProbeCompletion.edt {
            UiProbeCompletion.activate(project)
            val component = editor.contentComponent
            val focused = WindowManagerEx.getInstanceEx().getFocusedComponent(project)
            if (focused != null && focused !== component) {
                val gained = FocusEvent(component, FocusEvent.FOCUS_GAINED)
                component.focusListeners.filterIsInstance<FocusWatcher>().forEach { it.focusGained(gained) }
            }
        }
        // No focused component at all (the window is not the active one) is an answer the platform takes.
        UiProbeCompletion.await("the editor to be the window's focused component") {
            val focused = WindowManagerEx.getInstanceEx().getFocusedComponent(project)
            focused == null || focused === editor.contentComponent
        }
        UiProbeCompletion.edt { UiProbeCompletion.action(editor, IdeActions.ACTION_QUICK_JAVADOC) }
    }

    private fun close(text: String) {
        UiProbeCompletion.edt {
            panes().filter { text(it).contains(text) }.forEach { PopupUtil.getPopupContainerFor(it)?.cancel() }
        }
        UiProbeCompletion.await("documentation to close") { shown().none { it.contains(text) } }
    }

    /** The plain text of each documentation pane on screen, with its spacing made ordinary. */
    private fun shown(): List<String> = panes().map(::text).filter { it.isNotBlank() }

    private fun panes(): List<JEditorPane> = Window.getWindows().filter { it.isShowing }
        .flatMap { descendants(it).filterIsInstance<JEditorPane>().filter { pane -> pane.isShowing }.toList() }

    private fun text(pane: JEditorPane): String = pane.document.getText(0, pane.document.length)
        .replace(NO_BREAK_SPACE, ' ').replace(Regex("\\s+"), " ")

    private fun descendants(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(descendants(it)) }
    }

    private const val ABSENCE_MILLIS = 2000L
    private const val NO_BREAK_SPACE = '\u00A0'
}
