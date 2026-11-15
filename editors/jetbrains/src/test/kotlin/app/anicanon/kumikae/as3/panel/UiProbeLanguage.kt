package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.CodeInsightSettings
import com.intellij.openapi.actionSystem.IdeActions
import com.intellij.openapi.actionSystem.impl.SimpleDataContext
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.EditorFactory
import com.intellij.openapi.editor.VisualPosition
import com.intellij.openapi.editor.actionSystem.EditorActionManager
import com.intellij.openapi.editor.actionSystem.TypedAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileTypes.FileTypeManager
import com.intellij.openapi.project.ProjectManager
import com.intellij.testFramework.LightVirtualFile
import java.nio.file.Files
import java.nio.file.Path

/** Runs the installed editor handlers, not a model of what typing should do. */
object UiProbeLanguage {
    fun run(destination: Path) {
        val failures = mutableListOf<String>()
        ApplicationManager.getApplication().invokeAndWait {
            val project = ProjectManager.getInstance().defaultProject
            val file = LightVirtualFile("Completion.as", FileTypeManager.getInstance().getFileTypeByExtension("as"), "")
            val document = checkNotNull(FileDocumentManager.getInstance().getDocument(file))
            val editor = EditorFactory.getInstance().createEditor(document, project, file, false)
            fun reset(text: String, caret: Int = text.length) {
                WriteCommandAction.runWriteCommandAction(project) { document.setText(text) }
                editor.caretModel.moveToOffset(caret)
            }
            fun expect(name: String, text: String) {
                if (document.text != text) failures += "$name: expected <$text>, got <${document.text}>"
            }
            try {
                for ((opening, closing) in listOf('(' to ')', '[' to ']', '{' to '}', '"' to '"', '\'' to '\'')) {
                    WriteCommandAction.runWriteCommandAction(project) { document.setText("") }
                    editor.caretModel.moveToOffset(0)
                    type(editor, opening)
                    if (document.text != "$opening$closing") failures += "$opening pair: ${document.text}"
                    type(editor, closing)
                    if (document.text != "$opening$closing") failures += "$closing skip: ${document.text}"
                }
                reset("()", 1)
                action(editor, IdeActions.ACTION_EDITOR_BACKSPACE)
                expect("paired backspace", "")
                reset("abc", 0)
                type(editor, '(')
                expect("before identifier", "(abc")
                reset("// comment ")
                type(editor, '(')
                expect("comment", "// comment (")
                reset("var re:RegExp = /[.]/;", 18)
                type(editor, '(')
                expect("regex", "var re:RegExp = /[(.]/;")
                reset("{}", 1)
                action(editor, IdeActions.ACTION_EDITOR_ENTER)
                expect("block enter", "{\n    \n}")
                action(editor, IdeActions.ACTION_EDITOR_ENTER)
                expect("second block enter", "{\n    \n    \n}")
                reset("if (ready) /[.]/;", 13)
                type(editor, '(')
                expect("control regex", "if (ready) /[(.]/;")
                reset("var re:RegExp = /[.]/;", 18)
                type(editor, '\'')
                expect("quote in regex", "var re:RegExp = /['.]/;")
                val settings = CodeInsightSettings.getInstance()
                val brackets = settings.AUTOINSERT_PAIR_BRACKET
                val quotes = settings.AUTOINSERT_PAIR_QUOTE
                try {
                    settings.AUTOINSERT_PAIR_BRACKET = false
                    settings.AUTOINSERT_PAIR_QUOTE = false
                    reset("")
                    type(editor, '(')
                    expect("disabled bracket", "(")
                    reset("")
                    type(editor, '\'')
                    expect("disabled quote", "'")
                } finally {
                    settings.AUTOINSERT_PAIR_BRACKET = brackets
                    settings.AUTOINSERT_PAIR_QUOTE = quotes
                }
                reset(" \n ", 0)
                editor.caretModel.addCaret(VisualPosition(1, 0))
                type(editor, '(')
                expect("multiple carets", "() \n() ")
                editor.caretModel.removeSecondaryCarets()
                val times = mutableListOf<Double>()
                repeat(101) {
                    reset("")
                    val start = System.nanoTime()
                    type(editor, '(')
                    times += (System.nanoTime() - start) / 1_000_000.0
                }
                val warm = times.drop(1).sorted()
                Files.writeString(
                    destination.resolve("typing-times.txt"),
                    "cold=${times.first()} ms median=${warm[50]} ms p95=${warm[94]} ms max=${warm.last()} ms",
                )
            } finally {
                EditorFactory.getInstance().releaseEditor(editor)
            }
        }
        Files.writeString(destination.resolve("language.txt"), failures.joinToString("\n").ifEmpty { "passed" })
        check(failures.isEmpty()) { failures.joinToString("\n") }
    }

    private fun action(editor: Editor, id: String) {
        val context = SimpleDataContext.getProjectContext(checkNotNull(editor.project))
        WriteCommandAction.runWriteCommandAction(editor.project) {
            EditorActionManager.getInstance().getActionHandler(
                id,
            ).execute(editor, editor.caretModel.currentCaret, context)
        }
    }

    private fun type(editor: Editor, char: Char) {
        val context = SimpleDataContext.getProjectContext(checkNotNull(editor.project))
        TypedAction.getInstance().actionPerformed(editor, char, context)
    }
}
