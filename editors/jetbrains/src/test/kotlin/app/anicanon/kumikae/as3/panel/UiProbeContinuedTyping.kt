package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.lookup.LookupManager
import com.intellij.ide.DataManager
import com.intellij.openapi.actionSystem.IdeActions
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.actionSystem.TypedAction
import com.intellij.openapi.project.Project

internal object UiProbeContinuedTyping {
    fun run(project: Project, editor: Editor, observations: MutableList<String>) {
        for (backspace in listOf(false, true)) {
            for (replace in listOf(false, true)) checkAcceptance(project, editor, backspace, replace)
        }
        observations += "Typing and Backspace in an open lookup preserve Enter/Tab ranges and atomic Undo"
    }

    private fun checkAcceptance(project: Project, editor: Editor, backspace: Boolean, replace: Boolean) {
        val source = "package { import godot.*; public class CompletionProbe extends Node2D { " +
            "public function run():void { transform.origin.nosuffix; } } }"
        UiProbeCompletion.edt {
            LookupManager.getInstance(project).hideActiveLookup()
            WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(source) }
            editor.caretModel.moveToOffset(source.indexOf("nosuffix") + 2)
            UiProbeCompletion.invokeCompletion(project, editor)
        }
        awaitLookup(editor)
        UiProbeFocus.checkLookup(project, editor)
        UiProbeCompletion.edt {
            val context = DataManager.getInstance().getDataContext(editor.contentComponent)
            "rm".forEach { TypedAction.getInstance().actionPerformed(editor, it, context) }
            if (backspace) UiProbeCompletion.action(editor, IdeActions.ACTION_EDITOR_BACKSPACE)
        }
        awaitLookup(editor)
        UiProbeCompletion.edt {
            val before = editor.document.text
            val lookup = checkNotNull(LookupManager.getActiveLookup(editor))
            lookup.currentItem = lookup.items.single { it.lookupString.substringBefore('(') == "normalized" }
            UiProbeCompletion.action(
                editor,
                if (replace) IdeActions.ACTION_CHOOSE_LOOKUP_ITEM_REPLACE else IdeActions.ACTION_CHOOSE_LOOKUP_ITEM,
            )
            val expected = source.replace("nosuffix", if (replace) "normalized()" else "normalized()suffix")
            check(editor.document.text == expected) {
                "Wrong completion after typing (backspace=$backspace, replace=$replace): ${editor.document.text}"
            }
            UiProbeUndo.undo(project, editor)
            check(editor.document.text == before) { "Completion Undo lost typed text" }
        }
    }

    private fun awaitLookup(editor: Editor) = UiProbeCompletion.await("continued normalized lookup") {
        LookupManager.getActiveLookup(editor)?.items?.any { it.lookupString.substringBefore('(') == "normalized" } ==
            true
    }
}
