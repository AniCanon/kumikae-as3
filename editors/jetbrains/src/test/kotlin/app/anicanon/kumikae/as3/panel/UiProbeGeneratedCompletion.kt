package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.lookup.LookupManager
import com.intellij.ide.DataManager
import com.intellij.openapi.actionSystem.IdeActions
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.command.undo.UndoManager
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.actionSystem.TypedAction
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project

/** Additional import edits and generated declarations must be one undoable editor action. */
object UiProbeGeneratedCompletion {
    fun run(project: Project, editor: Editor, observations: MutableList<String>) {
        checkTyping(project, editor)
        observations += "Typing Vec into an open empty-prefix lookup offers Vector2 without reinvoking completion"
        checkEdit(
            project,
            editor,
            "package { public class CompletionProbe { public var point:Vec|; } }",
            "Vector2",
            "package { import godot.Vector2; public class CompletionProbe { public var point:Vector2; } }",
        )
        observations += "Auto-import and type replacement accepted together; Undo restored both"
        checkEdit(
            project,
            editor,
            "package { import godot.Node2D; public class CompletionProbe extends Node2D { override function _ph| } }",
            "_physicsProcess",
            "package { import godot.Node2D; public class CompletionProbe extends Node2D { " +
                "public override function _physicsProcess(delta:Number):void { throw new Error(\"Not implemented\"); } } }",
        )
        checkEdit(
            project,
            editor,
            "package { public class CompletionProbe { override function setProp| } }",
            "setPropertyIsEnumerable",
            "package { public class CompletionProbe { " +
                "public override function setPropertyIsEnumerable(name:String, isEnum:Boolean = true):void { " +
                "throw new Error(\"Not implemented\"); } } }",
        )
        observations += "Override declaration accepted; Undo restored partial declaration"
    }

    private fun checkTyping(project: Project, editor: Editor) {
        val source = "package { public class CompletionProbe { public var point:; } }"
        UiProbeCompletion.edt {
            LookupManager.getInstance(project).hideActiveLookup()
            WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(source) }
            editor.caretModel.moveToOffset(source.indexOf(':') + 1)
            UiProbeCompletion.invokeCompletion(project, editor)
        }
        UiProbeCompletion.await("empty-prefix lookup") { LookupManager.getActiveLookup(editor) != null }
        UiProbeCompletion.edt {
            val context = DataManager.getInstance().getDataContext(editor.contentComponent)
            "Vec".forEach { TypedAction.getInstance().actionPerformed(editor, it, context) }
        }
        UiProbeCompletion.await("typed automatic import") {
            LookupManager.getActiveLookup(editor)?.items?.any { it.lookupString == "Vector2" } == true
        }
    }

    private fun checkEdit(project: Project, editor: Editor, marked: String, name: String, expected: String) {
        val caret = marked.indexOf('|')
        val source = marked.replace("|", "")
        UiProbeCompletion.edt {
            LookupManager.getInstance(project).hideActiveLookup()
            WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(source) }
            editor.caretModel.moveToOffset(caret)
            UiProbeCompletion.invokeCompletion(project, editor)
        }
        UiProbeCompletion.await("generated $name lookup") {
            LookupManager.getActiveLookup(editor)?.items?.any {
                it.lookupString == name ||
                    it.lookupString.startsWith("$name(")
            } ==
                true
        }
        UiProbeCompletion.edt {
            val lookup = checkNotNull(LookupManager.getActiveLookup(editor))
            lookup.currentItem = lookup.items.single { it.lookupString == name || it.lookupString.startsWith("$name(") }
            UiProbeCompletion.action(editor, IdeActions.ACTION_CHOOSE_LOOKUP_ITEM_REPLACE)
        }
        UiProbeCompletion.await("generated $name edits") { editor.document.text.contains(name) }
        UiProbeCompletion.edt {
            // Clients indent multiline snippets differently; every non-whitespace character must survive.
            check(editor.document.text.filterNot(Char::isWhitespace) == expected.filterNot(Char::isWhitespace)) {
                "Generated $name text differs: ${editor.document.text}"
            }
            if (expected.contains("throw new Error")) {
                check(editor.caretModel.offset == editor.document.text.indexOf("throw new Error")) {
                    "Generated $name caret is not at the body"
                }
            }
            UndoManager.getInstance(project).undo(FileEditorManager.getInstance(project).selectedEditor)
            check(editor.document.text == source) {
                "Generated completion was not one undoable edit: ${editor.document.text}"
            }
        }
    }
}
