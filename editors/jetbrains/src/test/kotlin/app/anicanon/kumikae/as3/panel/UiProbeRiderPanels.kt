package app.anicanon.kumikae.as3.panel

import com.intellij.analysis.problemsView.toolWindow.HighlightingPanel
import com.intellij.analysis.problemsView.toolWindow.ProblemsViewPanelProvider
import com.intellij.openapi.Disposable
import com.intellij.openapi.extensions.ProjectExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.wm.ToolWindowManager
import java.util.function.Predicate

/** Inspects native ownership in the probe only; never writes the platform's disposal tree. */
internal object UiProbeRiderPanels {
    fun capture(project: Project): List<Disposable> {
        val providers = ProjectExtensionPointName<ProblemsViewPanelProvider>("com.intellij.problemsViewPanelProvider")
            .getPoint(project).extensionList
        check(providers.any { it.javaClass.name.endsWith(".RiderOwnedPanelProvider") }) {
            "Rider panel ownership provider was not installed"
        }
        val owner = checkNotNull(ToolWindowManager.getInstance(project).getToolWindow("Problems View")).disposable
        val panels = mutableListOf<Disposable>()
        val tree = Disposer.getTree()
        tree.assertNoReferenceKeptInTree(
            Predicate { candidate ->
                if (candidate is HighlightingPanel && candidate.project === project) panels += candidate
                false
            },
        )
        val backend = panels.filter { it.javaClass == HighlightingPanel::class.java }
        check(backend.isNotEmpty()) { "No native Current File backend panel found in the disposal tree" }
        val parent = tree.javaClass.getDeclaredMethod("getParentNode", Disposable::class.java)
        check(parent.trySetAccessible())
        backend.forEach { panel ->
            val node = checkNotNull(parent.invoke(tree, panel))
            val value = node.javaClass.getDeclaredMethod("getObject")
            check(value.trySetAccessible())
            check(value.invoke(node) === owner) {
                "Native Current File panel ${panel.javaClass.name} is not owned by $owner: " +
                    tree.printParentChainToRoot(panel)
            }
        }
        return panels
    }
}
