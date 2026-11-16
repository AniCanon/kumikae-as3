package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.completion.CompletionService
import com.intellij.codeInsight.lookup.LookupElement
import com.intellij.codeInsight.lookup.LookupManager
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.Project
import java.util.concurrent.TimeUnit

/**
 * Asks for completion until the lookup offers what is wanted. A request can end with no lookup and no
 * request sent to the server: a short dumb mode (the IDE pushing file properties after a project-model
 * change, seen in the log of a failing run) cancels a completion begun just before it. A person asks
 * again; so does the probe, only once nothing is running and the client can complete, and says so.
 */
internal object UiProbeLookup {
    fun complete(
        project: Project,
        editor: Editor,
        what: String,
        observations: MutableList<String>?,
        wanted: (LookupElement) -> Boolean,
    ) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(WAIT_SECONDS)
        var asked = 0
        while (System.nanoTime() < deadline) {
            var offered = false
            UiProbeCompletion.edt {
                val lookup = LookupManager.getActiveLookup(editor)
                offered = lookup?.items?.any(wanted) == true
                val idle = lookup == null && CompletionService.getCompletionService().currentCompletion == null
                if (!offered && idle && UiProbeLsp.canComplete(project, editor)) {
                    UiProbeCompletion.invokeCompletion(project, editor)
                    asked++
                }
            }
            if (offered) {
                if (asked > 1) observations?.add("$what: completion asked $asked times; earlier ones ended unanswered")
                return
            }
            Thread.sleep(POLL_MILLIS)
        }
        var shown: List<String>? = null
        UiProbeCompletion.edt { shown = LookupManager.getActiveLookup(editor)?.items?.map { it.lookupString } }
        error("Timed out waiting for $what after $asked completion requests; lookup: $shown")
    }

    private const val WAIT_SECONDS = 30L
    private const val POLL_MILLIS = 10L
}
