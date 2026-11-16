package app.anicanon.kumikae.as3.language

import com.intellij.analysis.problemsView.toolWindow.ProblemsViewPanelProvider
import com.intellij.analysis.problemsView.toolWindow.ProblemsViewTab
import com.intellij.openapi.Disposable
import com.intellij.openapi.util.Disposer

internal class RiderOwnedPanelProvider(
    private val owner: () -> Disposable,
    private val delegate: ProblemsViewPanelProvider,
) : ProblemsViewPanelProvider {
    override fun create(): ProblemsViewTab? {
        var created: ProblemsViewTab? = null
        var result: ProblemsViewTab? = null
        repairRider("Problems panel ownership") {
            try {
                val parent = owner()
                created = delegate.create()
                val panel = created
                check(panel !is Disposable || Disposer.tryRegister(parent, panel)) {
                    "Problems tool window was disposed before panel registration"
                }
                result = panel
            } finally {
                if (result == null) (created as? Disposable)?.let(Disposer::dispose)
            }
        }
        return result
    }
}
