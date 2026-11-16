package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.daemon.impl.DaemonCodeAnalyzerEx
import com.intellij.lang.annotation.HighlightSeverity
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.Project
import com.intellij.psi.PsiDocumentManager
import com.intellij.util.PlatformUtils

/** Checks actual editor errors after both a settled shrink and bursts that overtake publications. */
internal object UiProbeDiagnostics {
    fun run(project: Project, editor: Editor, observations: MutableList<String>) {
        val clean = "package { public class CompletionProbe { public function move(amount:int):void {} " +
            "public function run():void { move(1); } } }"
        val bad = "package { public class CompletionProbe { function run():void {\n" +
            "// diagnostic padding\n".repeat(80) + "missingDiagnosticProbe;\n} } }"
        replace(project, editor, bad)
        awaitError(project, editor)
        replace(project, editor, clean)
        UiProbeCompletion.await("diagnostics clear after document shrink") { errors(project, editor).isEmpty() }
        repeat(40) { index -> replace(project, editor, if (index % 2 == 0) bad else clean) }
        UiProbeCompletion.await("diagnostics clear after edit burst") { errors(project, editor).isEmpty() }
        replace(project, editor, bad)
        awaitError(project, editor)
        replace(project, editor, clean)
        UiProbeCompletion.await("diagnostics clear after recovery") { errors(project, editor).isEmpty() }
        observations += "Diagnostics appear, clear after shrink and forty edits, then appear and clear again"
        UiProbeLanguageFeatures.run(project, editor, observations)
        observations += "Parameter help, navigation and outline remain available after document shrink"
        if (!PlatformUtils.isRider()) UiProbeLsp4ijDiagnostics.run(project, editor, observations)
    }

    private fun replace(project: Project, editor: Editor, text: String) = UiProbeCompletion.edt {
        WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(text) }
        editor.caretModel.moveToOffset(0)
        PsiDocumentManager.getInstance(project).commitDocument(editor.document)
    }

    private fun awaitError(project: Project, editor: Editor) = UiProbeCompletion.await("current diagnostic") {
        errors(project, editor).any { it.contains("missingDiagnosticProbe") }
    }

    private fun errors(project: Project, editor: Editor): List<String> = buildList {
        DaemonCodeAnalyzerEx.processHighlights(
            editor.document,
            project,
            HighlightSeverity.ERROR,
            0,
            editor.document.textLength,
        ) {
            add(it.description.orEmpty())
            true
        }
    }
}
