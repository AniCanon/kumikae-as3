package app.anicanon.kumikae.as3.panel

import com.intellij.analysis.problemsView.toolWindow.HighlightingPanel
import com.intellij.analysis.problemsView.toolWindow.ProblemsViewPanelProvider
import com.intellij.openapi.Disposable
import com.intellij.openapi.extensions.ProjectExtensionPointName
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.ex.ProjectManagerEx
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
        val panels = findPanels(project)
        val tree = Disposer.getTree()
        val owner = checkNotNull(ToolWindowManager.getInstance(project).getToolWindow("Problems View")).disposable
        val parent = tree.javaClass.getDeclaredMethod("getParentNode", Disposable::class.java)
        check(parent.trySetAccessible())
        panels.filter { it.javaClass == HighlightingPanel::class.java }.forEach { panel ->
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

    fun observeClose(project: Project, observations: MutableList<String>): Int {
        val panels = try {
            findPanels(project).filter { it.javaClass == HighlightingPanel::class.java }
        } finally {
            check(ProjectManagerEx.getInstanceEx().forceCloseProject(project, false))
            check(project.isDisposed)
        }

        @Suppress("DEPRECATION")
        val survived = panels.count { !Disposer.isDisposed(it) }
        observations += "$survived of ${panels.size} Current File backend panels survived project close"
        return survived
    }

    private fun findPanels(project: Project): List<Disposable> {
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
        return panels
    }
}
