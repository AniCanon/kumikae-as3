package app.anicanon.kumikae.as3.language

import com.intellij.codeInsight.CodeInsightSettings
import com.intellij.codeInsight.editorActions.enter.EnterHandlerDelegate
import com.intellij.openapi.actionSystem.DataContext
import com.intellij.openapi.editor.Editor
import com.intellij.psi.PsiFile

/** The token-only PSI has no formatter; apply the same line-indent provider after the native Enter. */
class As3EnterHandler : EnterHandlerDelegate {
    override fun postProcessEnter(file: PsiFile, editor: Editor, context: DataContext): EnterHandlerDelegate.Result {
        if (file.language != As3Language || !CodeInsightSettings.getInstance().SMART_INDENT_ON_ENTER) {
            return EnterHandlerDelegate.Result.Continue
        }
        val document = editor.document
        val offset = editor.caretModel.offset
        val start = document.getLineStartOffset(document.getLineNumber(offset))
        if ((start until offset).any { !document.charsSequence[it].isWhitespace() }) {
            return EnterHandlerDelegate.Result.Continue
        }
        val indent = As3LineIndentProvider().getLineIndent(file.project, editor, file.language, offset)
            ?: return EnterHandlerDelegate.Result.Continue
        document.replaceString(start, offset, indent)
        editor.caretModel.moveToOffset(start + indent.length)
        return EnterHandlerDelegate.Result.Continue
    }
}
