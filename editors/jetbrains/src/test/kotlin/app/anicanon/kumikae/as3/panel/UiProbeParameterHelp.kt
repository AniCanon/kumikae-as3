package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.hint.HintManager
import com.intellij.codeInsight.hint.ParameterInfoControllerBase
import com.intellij.openapi.actionSystem.IdeActions
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer

/**
 * Parameter help at a call. An earlier request can leave a hint on screen whose owner no offset finds
 * (seen before a failing run: one visible controller, at no offset). Waiting for "a visible hint" would
 * then pass at once, and looking our own controller up before its hint shows disposes it: the platform
 * disposes a controller found with no visible hint. So hints are closed first, and only a visible hint
 * after that is taken as the answer to this request.
 */
internal object UiProbeParameterHelp {
    fun run(project: Project, editor: Editor, call: Int, observations: MutableList<String>) {
        UiProbeCompletion.edt {
            HintManager.getInstance().hideAllHints()
            check(!ParameterInfoControllerBase.existsWithVisibleHintForEditor(editor, false)) {
                "Parameter help from an earlier request is still shown"
            }
            UiProbeCompletion.activate(project)
            UiProbeCompletion.action(editor, IdeActions.ACTION_EDITOR_SHOW_PARAMETER_INFO)
        }
        UiProbeCompletion.await("parameter help") {
            ParameterInfoControllerBase.existsWithVisibleHintForEditor(editor, false)
        }
        UiProbeCompletion.edt {
            val controllers = (call..call + "move(amount)".length).mapNotNull {
                ParameterInfoControllerBase.findControllerAtOffset(editor, it)
            }.distinct()
            check(controllers.isNotEmpty()) { "Parameter help has no controller at the call" }
            check(controllers.any { it.objects.isNotEmpty() }) { "Parameter help contains no signature" }
            controllers.forEach { Disposer.dispose(it) }
            HintManager.getInstance().hideAllHints()
            observations += "Parameter help contains a signature"
        }
    }
}
