package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.ui.DialogWrapper
import java.awt.Window
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicReference
import javax.swing.Timer

/** Cancels only unsolicited feedback and onboarding dialogs in this test JVM, without changing IDE settings. */
internal object UiProbeFeedbackGuard {
    fun run(destination: Path, work: () -> Unit) {
        val failure = AtomicReference<Throwable>()
        val timer = Timer(250) {
            runCatching {
                Window.getWindows().filter { it.isShowing }.forEach { window ->
                    val dialog = DialogWrapper.findInstance(window)
                    if (dialog != null && dialog.javaClass.name in DISMISSIBLE_DIALOGS) {
                        dialog.close(DialogWrapper.CANCEL_EXIT_CODE)
                        Files.writeString(
                            destination.resolve("dismissed-dialogs.txt"),
                            "${dialog.javaClass.name} cancelled by sandbox probe\n",
                        )
                    }
                }
            }.exceptionOrNull()?.let { failure.compareAndSet(null, it) }
        }
        ApplicationManager.getApplication().invokeAndWait { timer.start() }
        try {
            work()
        } finally {
            ApplicationManager.getApplication().invokeAndWait { timer.stop() }
        }
        failure.get()?.let { throw it }
    }

    private val DISMISSIBLE_DIALOGS = setOf(
        "com.jetbrains.rider.plugins.appender.feedback.general.evaluation.LongEvaluationFeedbackDialog",
        "com.intellij.platform.ide.newUiOnboarding.newUi.NewUiOnboardingDialog",
    )
}
