package app.anicanon.kumikae.as3.panel

import com.intellij.notification.Notification
import com.intellij.notification.NotificationsManager
import com.intellij.openapi.project.Project

internal object UiProbeMissingClient {
    fun checkNotice(project: Project) {
        UiProbeCompletion.await("missing LSP4IJ notice") {
            val notices = NotificationsManager.getNotificationsManager()
                .getNotificationsOfType(Notification::class.java, project)
                .filter { it.groupId == "Kumikae AS3" && it.content.contains("LSP4IJ") }
            check(notices.size <= 1) { "Missing client produced duplicate notifications" }
            notices.singleOrNull()?.content?.contains("install", ignoreCase = true) == true
        }
    }
}
