package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.hint.ParameterInfoControllerBase
import com.intellij.ide.structureView.StructureViewTreeElement
import com.intellij.ide.structureView.TreeBasedStructureViewBuilder
import com.intellij.lang.LanguageStructureViewBuilder
import com.intellij.openapi.actionSystem.IdeActions
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.psi.PsiDocumentManager

/** Exercises feature routing when both client dependencies are available. */
internal object UiProbeLanguageFeatures {
    fun run(project: Project, editor: Editor, observations: MutableList<String>) {
        val source = "package { public class CompletionProbe { " +
            "public function move(amount:int):void {} " +
            "public function run():void { move(1); } } }"
        val call = source.lastIndexOf("move(")
        UiProbeCompletion.edt {
            WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(source) }
            PsiDocumentManager.getInstance(project).commitAllDocuments()
            editor.caretModel.moveToOffset(call + "move(".length)
        }
        UiProbeCompletion.edt {
            UiProbeCompletion.activate(project)
            UiProbeCompletion.action(editor, IdeActions.ACTION_EDITOR_SHOW_PARAMETER_INFO)
        }
        UiProbeCompletion.await("parameter help") {
            ParameterInfoControllerBase.existsWithVisibleHintForEditor(editor, false)
        }
        UiProbeCompletion.edt {
            val controllers = (call..call + "move(1)".length).mapNotNull {
                ParameterInfoControllerBase.findControllerAtOffset(editor, it)
            }.distinct()
            check(controllers.isNotEmpty()) { "Parameter help has no controller at the call" }
            check(controllers.any { it.objects.isNotEmpty() }) { "Parameter help contains no signature" }
            controllers.forEach { Disposer.dispose(it) }
            observations += "Parameter help contains a signature"
            editor.caretModel.moveToOffset(call + 1)
            UiProbeCompletion.action(editor, IdeActions.ACTION_GOTO_DECLARATION)
        }
        UiProbeCompletion.await("method declaration navigation") {
            editor.caretModel.offset in source.indexOf("move(")..source.indexOf("move(") + 4
        }
        observations += "Go to declaration reaches the method declaration"
        checkOutline(project, editor)
        observations += "Outline includes the declared method"
    }

    private fun checkOutline(project: Project, editor: Editor) {
        var model: com.intellij.ide.structureView.StructureViewModel? = null
        try {
            UiProbeCompletion.edt {
                val file = checkNotNull(PsiDocumentManager.getInstance(project).getPsiFile(editor.document))
                val builder = LanguageStructureViewBuilder.INSTANCE.getStructureViewBuilder(file)
                check(builder is TreeBasedStructureViewBuilder) { "Missing tree-based outline" }
                model = builder.createStructureViewModel(editor)
            }
            UiProbeCompletion.await("outline method") { containsMethod(checkNotNull(model).root, 0) }
        } finally {
            UiProbeCompletion.edt { model?.let { Disposer.dispose(it) } }
        }
    }

    private fun containsMethod(element: StructureViewTreeElement, depth: Int): Boolean =
        element.presentation.presentableText?.contains("move") == true ||
            (
                depth < 4 && element.children.filterIsInstance<StructureViewTreeElement>().any {
                    containsMethod(it, depth + 1)
                }
                )
}
