package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.editor.Editor
import com.intellij.openapi.project.DumbService
import com.intellij.openapi.project.Project
import com.intellij.util.PlatformUtils

/** Each client is referenced from a separate class so either optional dependency may be absent. */
internal object UiProbeLsp {
    fun checkDiagnostics(project: Project) {
        if (PlatformUtils.isRider()) UiProbeRiderDiagnostics.checkReceiveBoundary(project)
    }

    fun canComplete(project: Project, editor: Editor): Boolean = if (PlatformUtils.isRider()) {
        !DumbService.getInstance(project).isDumb
    } else {
        UiProbeLsp4ij.canComplete(project, editor)
    }

    fun ready(project: Project): Boolean {
        if (PlatformUtils.isRider()) {
            return UiProbeRiderLsp.ready(project)
        }
        return UiProbeLsp4ij.ready(project)
    }
}
