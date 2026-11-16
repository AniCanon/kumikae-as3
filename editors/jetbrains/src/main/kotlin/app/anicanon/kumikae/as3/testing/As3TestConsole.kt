package app.anicanon.kumikae.as3.testing

import com.intellij.execution.Executor
import com.intellij.execution.Location
import com.intellij.execution.PsiLocation
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.execution.testframework.actions.AbstractRerunFailedTestsAction
import com.intellij.execution.testframework.sm.runner.SMTRunnerConsoleProperties
import com.intellij.execution.testframework.sm.runner.SMTestLocator
import com.intellij.execution.testframework.sm.runner.ui.SMTRunnerConsoleView
import com.intellij.execution.ui.ConsoleView
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.psi.PsiManager
import com.intellij.psi.search.GlobalSearchScope
import java.net.URI

internal class As3TestConsole(private val configuration: As3TestsConfiguration, executor: Executor) :
    SMTRunnerConsoleProperties(configuration, "AS3", executor) {
    override fun getTestLocator() = object : SMTestLocator {
        override fun getLocation(
            protocol: String,
            path: String,
            project: Project,
            scope: GlobalSearchScope,
        ): List<Location<*>> {
            if (protocol != "file") return emptyList()
            val decoded = runCatching { URI("file://$path").path }.getOrNull() ?: return emptyList()
            val file = LocalFileSystem.getInstance().findFileByPath(decoded) ?: return emptyList()
            val psi = PsiManager.getInstance(project).findFile(file) ?: return emptyList()
            return listOf(PsiLocation(psi))
        }
    }

    override fun createRerunFailedTestsAction(console: ConsoleView): AbstractRerunFailedTestsAction =
        object : AbstractRerunFailedTestsAction(console) {
            init {
                init(this@As3TestConsole)
                setModelProvider { (console as SMTRunnerConsoleView).resultsViewer }
            }
            override fun getRunProfile(environment: ExecutionEnvironment): MyRunProfile {
                val copy = configuration.clone() as As3TestsConfiguration
                copy.options.cases = getFailedTests(configuration.project).map { it.name }.distinct().joinToString("\n")
                return object : MyRunProfile(copy) {
                    override fun getState(executor: Executor, environment: ExecutionEnvironment) =
                        copy.getState(executor, environment)
                }
            }
        }
}
