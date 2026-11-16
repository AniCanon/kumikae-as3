package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.lookup.LookupManager
import com.intellij.codeInsight.template.TemplateManager
import com.intellij.ide.DataManager
import com.intellij.openapi.actionSystem.IdeActions
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.actionSystem.TypedAction
import com.intellij.openapi.project.Project

/** Language blocks must use the client's ordinary snippet navigation and undo. */
object UiProbeStatementCompletion {
    private const val HEAD = "package { class CompletionProbe { function run():void { "
    private const val TAIL = " } } }"

    fun run(project: Project, editor: Editor, observations: MutableList<String>) {
        val cases = listOf(
            Triple("try", "try {\n\t\n} catch (error:Error) {\n\tthrow error;\n}", 4),
            Triple("if", "if (condition) {\n\t\n}", 2),
            Triple("while", "while (condition) {\n\t\n}", 2),
            Triple("do", "do {\n\t\n} while (condition);", 2),
            Triple("for", "for (var i:int = 0; i < count; i++) {\n\t\n}", 3),
            Triple("for each", "for each (var item:* in items) {\n\t\n}", 3),
            Triple("switch", "switch (value) {\n\tcase 0:\n\t\t\n\t\tbreak;\n\tdefault:\n\t\t\n\t\tbreak;\n}", 4),
        )
        for ((name, text, stops) in cases) {
            val source = accept(project, editor, name, text)
            UiProbeCompletion.edt {
                UiProbeUndo.undo(project, editor)
                check(editor.document.text == source) {
                    "$name insertion was not one undoable edit: ${editor.document.text}; expected $source"
                }
            }
            accept(project, editor, name, text)
            UiProbeCompletion.edt {
                repeat(stops) { UiProbeCompletion.action(editor, IdeActions.ACTION_EDITOR_NEXT_TEMPLATE_VARIABLE) }
            }
            awaitExit(project, editor, name)
        }
        accept(project, editor, "try", cases.first().second)
        UiProbeCompletion.edt {
            UiProbeCompletion.action(editor, IdeActions.ACTION_EDITOR_NEXT_TEMPLATE_VARIABLE)
            check(editor.selectionModel.selectedText == "error") { "Catch name is not selected" }
            val context = DataManager.getInstance().getDataContext(editor.contentComponent)
            "caught".forEach { TypedAction.getInstance().actionPerformed(editor, it, context) }
            UiProbeCompletion.action(editor, IdeActions.ACTION_EDITOR_NEXT_TEMPLATE_VARIABLE)
            check(editor.selectionModel.selectedText == "Error") { "Catch type is not selected" }
            check(editor.document.text.contains("catch (caught:Error)")) { editor.document.text }
            check(editor.document.text.contains("throw caught;")) { "Catch name mirror did not update" }
            repeat(2) { UiProbeCompletion.action(editor, IdeActions.ACTION_EDITOR_NEXT_TEMPLATE_VARIABLE) }
        }
        awaitExit(project, editor, "try mirror")
        observations +=
            "Seven language blocks preserve text, Tab navigation and Undo; catch-name edits update the rethrow"
    }

    private fun accept(project: Project, editor: Editor, name: String, expected: String): String {
        val prefix = name.substringBefore(' ').take(2)
        val source = HEAD + prefix + "suffix" + TAIL
        UiProbeCompletion.edt {
            LookupManager.getInstance(project).hideActiveLookup()
            WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(source) }
            editor.caretModel.moveToOffset(HEAD.length + prefix.length)
            UiProbeCompletion.invokeCompletion(project, editor)
        }
        try {
            UiProbeCompletion.await("$name language template") {
                LookupManager.getActiveLookup(editor)?.items?.any { matches(it.lookupString, name) } == true
            }
        } catch (failure: IllegalStateException) {
            var offered: List<String>? = null
            UiProbeCompletion.edt { offered = LookupManager.getActiveLookup(editor)?.items?.map { it.lookupString } }
            error("$name template lookup failed: $offered; ${failure.message}")
        }
        UiProbeCompletion.edt {
            val lookup = checkNotNull(LookupManager.getActiveLookup(editor))
            lookup.currentItem = checkNotNull(lookup.items.singleOrNull { matches(it.lookupString, name) }) {
                "Missing $name among ${lookup.items.map { it.lookupString }}"
            }
            UiProbeCompletion.action(editor, IdeActions.ACTION_CHOOSE_LOOKUP_ITEM_REPLACE)
            check(
                editor.document.text.filterNot(Char::isWhitespace) ==
                    (HEAD + expected + TAIL).filterNot(Char::isWhitespace),
            ) {
                "Incorrect $name block: ${editor.document.text}"
            }
            check(TemplateManager.getInstance(project).getActiveTemplate(editor) != null) { "No $name template" }
            if (name == "try" || name == "do") {
                val bodyStart = editor.document.text.indexOf('\n') + 1
                val bodyEnd = editor.document.text.indexOf('\n', bodyStart)
                check(editor.caretModel.offset == bodyEnd) {
                    "Body is not the first stop: caret=${editor.caretModel.offset}, body=$bodyEnd, ${editor.document.text}"
                }
            } else {
                val first = when (name) {
                    "for" -> "i"
                    "for each" -> "item"
                    "switch" -> "value"
                    else -> "condition"
                }
                check(editor.selectionModel.selectedText == first) { "$name first placeholder is not selected" }
            }
        }
        return source
    }

    // Rider exposes the snippet as its lookup string; LSP4IJ exposes the item label.
    private fun matches(lookup: String, name: String): Boolean =
        lookup == name || lookup.startsWith("$name {") || lookup.startsWith("$name (")

    private fun awaitExit(project: Project, editor: Editor, name: String) {
        UiProbeCompletion.await("$name template exit") {
            TemplateManager.getInstance(project).getActiveTemplate(editor) == null &&
                editor.caretModel.offset == editor.document.text.length - TAIL.length
        }
    }
}
