package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.application.ApplicationManager
import java.util.concurrent.TimeUnit

internal object UiProbeDispatch {
    internal fun edt(action: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(action)

    internal fun await(name: String, state: () -> String = { "" }, condition: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
        while (System.nanoTime() < deadline) {
            var ready = false
            edt { ready = condition() }
            if (ready) return
            Thread.sleep(10)
        }
        var found = ""
        edt { found = state() }
        error("Timed out waiting for $name" + if (found.isEmpty()) "" else ": $found")
    }
}
