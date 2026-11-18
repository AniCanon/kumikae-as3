package app.anicanon.kumikae.as3.testing

import app.anicanon.kumikae.as3.run.KumikaeRunType
import com.intellij.execution.DefaultExecutionTarget
import com.intellij.execution.ExecutionException
import com.intellij.execution.RunManager
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.runners.ExecutionEnvironmentBuilder
import com.intellij.icons.AllIcons
import com.intellij.openapi.actionSystem.ActionUpdateThread
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.project.DumbAwareAction
import com.intellij.openapi.ui.Messages
import com.intellij.psi.PsiElement
import com.intellij.psi.SmartPointerManager

class RunGameTestsAction() :
    DumbAwareAction("Run Game Tests", "Run tests in this selection", AllIcons.Actions.Execute) {
    private var pointer: com.intellij.psi.SmartPsiElementPointer<PsiElement>? = null

    internal constructor(element: PsiElement, label: String) : this() {
        pointer = SmartPointerManager.createPointer(element)
        templatePresentation.text = label
    }

    private fun element(event: AnActionEvent) = pointer?.element ?: event.getData(CommonDataKeys.PSI_ELEMENT)
    override fun getActionUpdateThread() = ActionUpdateThread.BGT
    override fun update(event: AnActionEvent) {
        event.presentation.isEnabledAndVisible =
            element(event)?.let { TestDiscovery.forElement(it).isNotEmpty() } == true
    }
    override fun actionPerformed(event: AnActionEvent) {
        val selected = element(event) ?: return
        val project = selected.project
        val factory = KumikaeRunType.factory(KumikaeRunType.Kind.TESTS)
        val settings = RunManager.getInstance(project).createConfiguration("Game tests", factory)
        if (!configureTestScope(settings.configuration as As3TestsConfiguration, selected)) return
        try {
            ExecutionEnvironmentBuilder.create(DefaultRunExecutor.getRunExecutorInstance(), settings)
                .target(DefaultExecutionTarget.INSTANCE).buildAndExecute()
        } catch (error: ExecutionException) {
            Messages.showErrorDialog(project, error.message ?: "Could not start the test runner.", "Game Tests")
        }
    }
}
